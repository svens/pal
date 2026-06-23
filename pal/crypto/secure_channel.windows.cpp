/**
 * \file pal/crypto/secure_channel.windows.cpp
 * Windows SChannel implementation of the pal secure channel (TLS/DTLS).
 *
 * One SCHANNEL_CRED credential (via UNISP_NAME_W) serves both transports; the protocol family is
 * selected purely by the *_REQ_DATAGRAM context flag at handshake time, not by the credential. The
 * handshake, EncryptMessage/DecryptMessage record layer, and shutdown are driven through caller-
 * provided byte spans with no SChannel-side allocation.
 *
 * Two DTLS-specific quirks vs. stream TLS, both SChannel limitations rather than bugs:
 *
 *  - close_notify: SChannel cannot generate a DTLS close_notify (ApplyControlToken + ISC/ASC
 *    renegotiates instead of shutting down). A datagram close() is therefore a best-effort local
 *    no-op; the peer detects closure via the transport. The receive path still reports peer_closed
 *    if a peer that does emit close_notify (e.g. OpenSSL) sends one.
 *
 *  - ALPN on the acceptor: the DTLS server negotiates ALPN correctly on the wire (the client sees
 *    the selection, and a protocol mismatch is rejected at handshake time), but it cannot introspect
 *    its own choice afterwards — SECPKG_ATTR_APPLICATION_PROTOCOL reports "none". So a datagram
 *    acceptor's selected_protocol() is empty.
 *
 * DTLS anti-amplification is not yet wired up: the HelloVerifyRequest cookie binds to a constant
 * placeholder address (see TODO(dtls-cookie)), not the caller's real peer endpoint.
 */

#include <pal/crypto/__crypto.hpp>

#if __pal_crypto_windows

#include <pal/crypto/__certificate.hpp>
#include <pal/crypto/__secure_channel.hpp>
#include <pal/crypto/secure_channel.hpp>
#include <pal/memory.hpp>
#include <algorithm>
#include <array>
#include <cstring>
#include <span>
#include <string>
#include <tuple>
#include <vector>

#ifndef SECURITY_WIN32
	#define SECURITY_WIN32
#endif
#ifndef SCHANNEL_USE_BLACKLISTS
	#define SCHANNEL_USE_BLACKLISTS
#endif
#include <security.h>
#include <schannel.h>

namespace pal::crypto
{

namespace
{

// SecBuffer / SecBufferDesc {{{1
struct sec_buffer: ::SecBuffer
{
	sec_buffer (ULONG type = SECBUFFER_EMPTY, void *data = nullptr, size_t size = 0) noexcept
		: ::SecBuffer{static_cast<ULONG>(size), type, data}
	{
	}

	sec_buffer (ULONG type, std::span<std::byte> b) noexcept
		: sec_buffer{type, b.data(), b.size()}
	{
	}

	// Input-only buffers: SChannel reads (never mutates) the payload, so the const_cast is sound and
	// lives in one audited place instead of scattered across call sites.
	sec_buffer (ULONG type, std::span<const std::byte> b) noexcept
		: sec_buffer{type, const_cast<std::byte *>(b.data()), b.size()}
	{
	}
};

struct sec_buffer_desc: ::SecBufferDesc
{
	template <size_t N>
	sec_buffer_desc (sec_buffer (&buffers)[N]) noexcept
		: ::SecBufferDesc{SECBUFFER_VERSION, static_cast<ULONG>(N), buffers}
	{
	}

	sec_buffer_desc (sec_buffer *buffers, ULONG count) noexcept
		: ::SecBufferDesc{SECBUFFER_VERSION, count, buffers}
	{
	}
};

// (D)TLS context request flags {{{1

// clang-format off

constexpr DWORD isc_stream_flags =
	ISC_REQ_SEQUENCE_DETECT |
	ISC_REQ_REPLAY_DETECT |
	ISC_REQ_CONFIDENTIALITY |
	ISC_REQ_STREAM;

constexpr DWORD isc_dtls_flags =
	ISC_REQ_CONFIDENTIALITY |
	ISC_REQ_EXTENDED_ERROR |
	ISC_REQ_DATAGRAM;

constexpr DWORD asc_stream_flags =
	ASC_REQ_SEQUENCE_DETECT |
	ASC_REQ_REPLAY_DETECT |
	ASC_REQ_CONFIDENTIALITY |
	ASC_REQ_STREAM;

constexpr DWORD asc_dtls_flags =
	ASC_REQ_CONFIDENTIALITY |
	ASC_REQ_EXTENDED_ERROR |
	ASC_REQ_DATAGRAM;

// clang-format on

const wchar_t *to_wide (std::string_view utf8, std::span<wchar_t> buf) noexcept //{{{1
{
	const int n = ::MultiByteToWideChar(
		CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), buf.data(), static_cast<int>(buf.size() - 1)
	);
	return n > 0 ? buf.data() : nullptr;
}

std::error_code map_handshake_error (SECURITY_STATUS ss) noexcept //{{{1
{
	switch (ss)
	{
		case SEC_E_WRONG_PRINCIPAL:
			return make_error_code(secure_channel_errc::peer_hostname_mismatch);

		case SEC_E_UNTRUSTED_ROOT:
		case SEC_E_CERT_EXPIRED:
		case SEC_E_CERT_UNKNOWN:
		case CERT_E_UNTRUSTEDROOT:
		case CERT_E_EXPIRED:
		case CERT_E_CHAINING:
			return make_error_code(secure_channel_errc::peer_verification_failed);

		case SEC_E_APPLICATION_PROTOCOL_MISMATCH:
			return make_error_code(secure_channel_errc::no_application_protocol);

		case SEC_E_NO_CREDENTIALS:
		case SEC_E_INCOMPLETE_CREDENTIALS:
			return make_error_code(secure_channel_errc::client_certificate_required);

		default:
			return make_error_code(secure_channel_errc::handshake_failed);
	}
}

/// Shared session state held by handshake_channel::impl_type and migrated to
/// connected_channel::impl_type on handshake completion.
struct session_state //{{{1
{
	__secure_channel::context_ptr config;
	CtxtHandle security_ctx = {};
	const verify_relax relax;
	const bool is_datagram;

	static constexpr size_t max_peer_name = 255;
	std::array<char, max_peer_name> peer_name_buf{};
	size_t peer_name_size = 0;

	session_state (
		__secure_channel::context_ptr config, bool is_datagram, verify_relax relax, std::string_view peer_name
	) noexcept
		: config{std::move(config)}
		, relax{relax}
		, is_datagram{is_datagram}
		, peer_name_size{peer_name.size()}
	{
		SecInvalidateHandle(&security_ctx);
		std::memcpy(peer_name_buf.data(), peer_name.data(), peer_name.size());
	}

	~session_state () noexcept
	{
		if (SecIsValidHandle(&security_ctx))
		{
			::DeleteSecurityContext(&security_ctx);
		}
	}

	session_state (const session_state &) = delete;
	session_state &operator= (const session_state &) = delete;

	std::string_view peer_name () const noexcept
	{
		return {peer_name_buf.data(), peer_name_size};
	}

	// Drive one ISC/ASC handshake step into the caller's buffers; returns the SECURITY_STATUS and marks the
	// security context valid once established.
	SECURITY_STATUS
	do_step (std::span<const std::byte> in, sec_buffer_desc &in_desc, sec_buffer_desc &out_desc) noexcept;

	// Manual peer-cert verification SChannel skips after a completed handshake; empty on success.
	std::error_code verify_peer () noexcept;
};
using session_state_ptr = std::unique_ptr<session_state>;

//}}}1

} // namespace

struct handshake_channel::impl_type //{{{1
{
	session_state_ptr state;

	explicit impl_type (session_state_ptr state) noexcept
		: state{std::move(state)}
	{
	}
};

handshake_channel::handshake_channel () = default;
handshake_channel::handshake_channel (handshake_channel &&) noexcept = default;
handshake_channel &handshake_channel::operator= (handshake_channel &&) noexcept = default;
handshake_channel::~handshake_channel () noexcept = default;

handshake_channel::handshake_channel (impl_ptr impl) noexcept
	: impl_{std::move(impl)}
{
}

struct connected_channel::impl_type //{{{1
{
	session_state_ptr state;

	SecPkgContext_StreamSizes sizes = {};
	bool closed = false;
	bool close_done = false;

	// Capacity for one inbound (D)TLS record on the wire. Sized for the protocol maximum a peer may send.
	static constexpr size_t record_cap = 16 * 1024 + 2048 + 13;

	// Inbound ciphertext buffer; DecryptMessage decrypts in place here. cipher_pending is the partial-record
	// accumulation count. After a decrypt the plaintext lives in this same buffer, and [plain_first,
	// plain_last) is the tail not yet copied to the caller (offsets into cipher_buf). That tail is safe to
	// leave here because the drain path returns before any new ciphertext is accumulated, so it is never
	// clobbered while pending.
	std::array<std::byte, record_cap> cipher_buf{};
	size_t cipher_pending = 0;
	size_t plain_first = 0, plain_last = 0;

	struct alpn
	{
		static constexpr size_t max_id_size = 256;
		std::array<char, max_id_size> storage{};
		std::string_view view;
	} alpn{};

	explicit impl_type (session_state_ptr state) noexcept
		: state{std::move(state)}
	{
		std::ignore = ::QueryContextAttributesW(&this->state->security_ctx, SECPKG_ATTR_STREAM_SIZES, &sizes);

		// Negotiated ALPN. Works for stream (both roles) and the datagram client. The datagram *server*
		// cannot introspect its own selection — SChannel reports ProtoNegoStatus "none" even though it
		// selected and sent the protocol — so a datagram acceptor's selected_protocol() is empty.
		SecPkgContext_ApplicationProtocol alpn_attr = {};
		const auto ss = ::QueryContextAttributesW(
			&this->state->security_ctx, SECPKG_ATTR_APPLICATION_PROTOCOL, &alpn_attr
		);

		// clang-format off
		if (ss == SEC_E_OK
			&& alpn_attr.ProtoNegoStatus == SecApplicationProtocolNegotiationStatus_Success
			&& alpn_attr.ProtocolIdSize > 0 && alpn_attr.ProtocolIdSize <= alpn.max_id_size)
		{
			std::memcpy(alpn.storage.data(), alpn_attr.ProtocolId, alpn_attr.ProtocolIdSize);
			alpn.view = {alpn.storage.data(), alpn_attr.ProtocolIdSize};
		}
		// clang-format on
	}
};

connected_channel::connected_channel () = default;
connected_channel::connected_channel (connected_channel &&) noexcept = default;
connected_channel &connected_channel::operator= (connected_channel &&) noexcept = default;
connected_channel::~connected_channel () noexcept = default;

connected_channel::connected_channel (impl_ptr impl) noexcept
	: impl_{std::move(impl)}
{
}

//}}}1

namespace __secure_channel
{

struct attorney //{{{1
{
	static auto to_sys (const certificate &c) noexcept
	{
		return c.impl_ ? c.impl_->x509.get() : nullptr;
	}

	static ::NCRYPT_KEY_HANDLE to_sys (const key &k) noexcept
	{
		if (!k.impl_)
		{
			return 0;
		}
		if (const auto *nk = std::get_if<::NCRYPT_KEY_HANDLE>(&k.impl_->pkey))
		{
			return *nk;
		}
		return 0;
	}

	// clang-format off

	template <typename... Args>
	static result<handshake_channel> emit_handshake_channel (Args &&...args) noexcept
	{
		return pal::make_unique<handshake_channel::impl_type>(std::forward<Args>(args)...).transform([] (auto impl)
		{
			return handshake_channel{std::move(impl)};
		});
	}

	template <typename... Args>
	static result<connected_channel> emit_connected_channel (Args &&...args) noexcept
	{
		return pal::make_unique<connected_channel::impl_type>(std::forward<Args>(args)...).transform([] (auto impl)
		{
			return connected_channel{std::move(impl)};
		});
	}

	// clang-format on
};

struct context //{{{1
{
	const kind kind;
	CredHandle cred = {};
	bool require_client_certificate = false;

	// Custom trusted-root store; NULL means use system trust (set as hRootStore in SCHANNEL_CRED).
	HCERTSTORE root_store = nullptr;

	// Max retained cert chain (leaf + intermediates). Fixed capacity so make_context allocates nothing in
	// its noexcept body; a deeper chain is rejected as invalid_configuration. Bounds both cert arrays.
	static constexpr size_t max_chain_size = 8;

	// Keep cert material alive so the NCRYPT key persists on disk for the credential's lifetime.
	std::array<certificate, max_chain_size> chain_refs{};
	size_t chain_count = 0;

	// CurrentUser/CA system store, plus the intermediate-CA contexts we added to it. When building the
	// outgoing Certificate message SChannel only ships intermediates it finds in a system store — a spike
	// proved it does not consult a private in-memory store during the handshake — so we add ours to this
	// shared, persistent store and delete them again on destruction.
	struct ca_store
	{
		HCERTSTORE store = nullptr;
		std::array<PCCERT_CONTEXT, max_chain_size> certs{};
		size_t count = 0;

		~ca_store () noexcept
		{
			if (store)
			{
				for (auto *cert: std::span{certs}.first(count))
				{
					::CertDeleteCertificateFromStore(cert);
				}
				::CertCloseStore(store, 0);
			}
		}

		// Best-effort: a failed open or add just means that intermediate is not sent.
		void add_intermediates (std::span<const certificate> intermediates) noexcept;
	} ca_store{};

	// Offered ALPN list, prebuilt once at make_context into SChannel's SEC_APPLICATION_PROTOCOLS form and
	// presented as a read-only SECBUFFER_APPLICATION_PROTOCOLS input on every handshake step. buffer_size is
	// 0 when no ALPN is configured.
	struct alpn
	{
		static constexpr size_t max_wire_size = 256;
		static constexpr size_t alpn_struct_size = sizeof(DWORD) + sizeof(SEC_APPLICATION_PROTOCOL_LIST);
		alignas(DWORD) std::array<BYTE, max_wire_size + alpn_struct_size> buffer{};
		ULONG buffer_size = 0;

		// Encode \a protocols into buffer/buffer_size. Returns false on an empty/oversized name or overflow.
		[[nodiscard]] bool encode (std::span<const std::string_view> protocols) noexcept;
	} alpn{};

	// Manually verify a peer cert chain against this context's trust (custom root_store if set, else system
	// trust) after a manual-validation handshake; also checks the hostname when peer_name is non-empty.
	// Empty error_code on success.
	std::error_code verify_peer_cert (
		const cert_ptr &peer_cert, std::string_view peer_name, verify_relax relax, DWORD auth_type
	) const noexcept;

	explicit context (enum kind kind) noexcept
		: kind{kind}
	{
		SecInvalidateHandle(&cred);
	}

	~context () noexcept
	{
		if (SecIsValidHandle(&cred))
		{
			::FreeCredentialsHandle(&cred);
		}
		if (root_store)
		{
			::CertCloseStore(root_store, 0);
		}
	}
};

void context::ca_store::add_intermediates (std::span<const certificate> intermediates) noexcept //{{{1
{
	if (intermediates.empty())
	{
		return;
	}

	store = ::CertOpenStore(CERT_STORE_PROV_SYSTEM_W, 0, 0, CERT_SYSTEM_STORE_CURRENT_USER, L"CA");
	if (!store)
	{
		return;
	}

	for (const auto &cert: intermediates)
	{
		if (auto c = attorney::to_sys(cert))
		{
			PCCERT_CONTEXT added = nullptr;
			auto rc = ::CertAddCertificateContextToStore(store, c, CERT_STORE_ADD_REPLACE_EXISTING, &added);
			if (rc && added)
			{
				certs[count++] = added;
			}
		}
	}
}

bool context::alpn::encode (std::span<const std::string_view> protocols) noexcept //{{{1
{
	auto &list = *reinterpret_cast<SEC_APPLICATION_PROTOCOLS *>(buffer.data());
	BYTE *wire = list.ProtocolLists[0].ProtocolList;

	const auto n = encode_alpn_wire(protocols, std::span{reinterpret_cast<std::byte *>(wire), max_wire_size});
	if (!n)
	{
		return false;
	}

	if (*n == 0)
	{
		buffer_size = 0;
		return true;
	}

	list.ProtocolLists[0].ProtoNegoExt = SecApplicationProtocolNegotiationExt_ALPN;
	list.ProtocolLists[0].ProtocolListSize = static_cast<WORD>(*n);
	list.ProtocolListsSize = static_cast<DWORD>(offsetof(SEC_APPLICATION_PROTOCOL_LIST, ProtocolList) + *n);
	buffer_size = static_cast<ULONG>(sizeof(DWORD) + list.ProtocolListsSize);
	return true;
}

std::error_code context::verify_peer_cert ( //{{{1
	const cert_ptr &peer_cert,
	std::string_view peer_name,
	verify_relax relax,
	DWORD auth_type) const noexcept
{
	// Build a chain engine that trusts only root_store as roots (replaces system root store).
	// The intermediate CA will be in peer_cert->hCertStore (received in TLS Certificate message)
	// and does not need to be in the engine's additional stores.
	HCERTCHAINENGINE engine = nullptr;
	if (root_store)
	{
		CERT_CHAIN_ENGINE_CONFIG cfg = {};
		cfg.cbSize = sizeof(cfg);
		cfg.hExclusiveRoot = root_store;
		if (!::CertCreateCertificateChainEngine(&cfg, &engine))
		{
			return make_error_code(secure_channel_errc::peer_verification_failed);
		}
	}

	CERT_CHAIN_PARA chain_para = {};
	chain_para.cbSize = sizeof(chain_para);

	PCCERT_CHAIN_CONTEXT chain_ctx = nullptr;
	const BOOL ok = ::CertGetCertificateChain(
		engine, peer_cert.get(), nullptr, nullptr, &chain_para, 0, nullptr, &chain_ctx
	);

	if (engine)
	{
		::CertFreeCertificateChainEngine(engine);
	}

	if (!ok || !chain_ctx)
	{
		return make_error_code(secure_channel_errc::peer_verification_failed);
	}

	DWORD trust_err = chain_ctx->TrustStatus.dwErrorStatus;

	if (any(relax & verify_relax::self_signed) && (trust_err & CERT_TRUST_IS_UNTRUSTED_ROOT))
	{
		if (chain_ctx->cChain > 0 && chain_ctx->rgpChain[0]->cElement > 0)
		{
			const auto *leaf = chain_ctx->rgpChain[0]->rgpElement[0]->pCertContext;
			const bool self_signed = ::CertCompareCertificateName(
				X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
				&leaf->pCertInfo->Subject,
				&leaf->pCertInfo->Issuer
			);
			if (self_signed != FALSE)
			{
				trust_err &= ~CERT_TRUST_IS_UNTRUSTED_ROOT;
				trust_err &= ~CERT_TRUST_IS_PARTIAL_CHAIN;
			}
		}
	}

	std::error_code result;

	if (trust_err != 0)
	{
		result = make_error_code(secure_channel_errc::peer_verification_failed);
	}
	else if (!peer_name.empty())
	{
		std::array<wchar_t, 256> wide{};
		to_wide(peer_name, wide);

		SSL_EXTRA_CERT_CHAIN_POLICY_PARA ssl_policy = {};
		ssl_policy.cbSize = sizeof(ssl_policy);
		ssl_policy.dwAuthType = auth_type;
		ssl_policy.pwszServerName = wide.data();

		CERT_CHAIN_POLICY_PARA policy_para = {};
		policy_para.cbSize = sizeof(policy_para);
		policy_para.pvExtraPolicyPara = &ssl_policy;

		CERT_CHAIN_POLICY_STATUS policy_status = {};
		policy_status.cbSize = sizeof(policy_status);

		::CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_SSL, chain_ctx, &policy_para, &policy_status);

		if (policy_status.dwError != 0)
		{
			result = make_error_code(
				policy_status.dwError == CERT_E_CN_NO_MATCH
					? secure_channel_errc::peer_hostname_mismatch
					: secure_channel_errc::peer_verification_failed
			);
		}
	}

	::CertFreeCertificateChain(chain_ctx);
	return result;
}

//}}}1

namespace
{

HCERTSTORE make_cert_store (std::span<const certificate> roots) noexcept //{{{1
{
	HCERTSTORE store = ::CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, 0, nullptr);
	if (!store)
	{
		return nullptr;
	}

	for (const auto &root: roots)
	{
		auto ctx = attorney::to_sys(root);
		if (!ctx)
		{
			::CertCloseStore(store, 0);
			return nullptr;
		}
		if (!::CertAddCertificateContextToStore(store, ctx, CERT_STORE_ADD_REPLACE_EXISTING, nullptr))
		{
			::CertCloseStore(store, 0);
			return nullptr;
		}
	}

	return store;
}

//}}}1

} // namespace

result<context_ptr> make_context (transport t, const acceptor_options &opts) noexcept //{{{1
{
	if (attorney::to_sys(opts.private_key) == 0)
	{
		return make_unexpected(secure_channel_errc::invalid_configuration);
	}
	if (opts.certificate_chain.empty() || opts.certificate_chain.size() > context::max_chain_size)
	{
		return make_unexpected(secure_channel_errc::invalid_configuration);
	}

	const kind k = make_kind(t, true);
	auto ctx_result = pal::make_shared<context>(k);
	if (!ctx_result)
	{
		return pal::unexpected{ctx_result.error()};
	}

	auto &ctx = **ctx_result;
	ctx.require_client_certificate = opts.require_client_certificate;

	std::ranges::copy(opts.certificate_chain, ctx.chain_refs.begin());
	ctx.chain_count = opts.certificate_chain.size();
	ctx.ca_store.add_intermediates(opts.certificate_chain.subspan(1));

	if (!ctx.alpn.encode(opts.supported_protocols))
	{
		return make_unexpected(secure_channel_errc::invalid_configuration);
	}

	auto leaf_cert = attorney::to_sys(ctx.chain_refs.front());
	if (!leaf_cert)
	{
		return make_unexpected(secure_channel_errc::invalid_configuration);
	}

	SCHANNEL_CRED cred = {};
	cred.dwVersion = SCHANNEL_CRED_VERSION;
	cred.cCreds = 1;
	cred.paCred = &leaf_cert;
	if (opts.require_client_certificate)
	{
		cred.dwFlags = SCH_CRED_NO_SYSTEM_MAPPER;
		if (!opts.trusted_roots.empty())
		{
			ctx.root_store = make_cert_store(opts.trusted_roots);
			if (!ctx.root_store)
			{
				return make_unexpected(secure_channel_errc::invalid_configuration);
			}
			cred.hRootStore = ctx.root_store;
		}
	}

	const SECURITY_STATUS ss = ::AcquireCredentialsHandleW(
		nullptr,
		const_cast<SEC_WCHAR *>(UNISP_NAME_W),
		SECPKG_CRED_INBOUND,
		nullptr,
		&cred,
		nullptr,
		nullptr,
		&ctx.cred,
		nullptr
	);

	if (ss != SEC_E_OK)
	{
		SecInvalidateHandle(&ctx.cred);
		return make_unexpected(secure_channel_errc::invalid_configuration);
	}

	return *ctx_result;
}

result<context_ptr> make_context (transport t, const connector_options &opts) noexcept //{{{1
{
	if (!opts.certificate_chain.empty() && attorney::to_sys(opts.private_key) == 0)
	{
		return make_unexpected(secure_channel_errc::invalid_configuration);
	}
	if (opts.certificate_chain.size() > context::max_chain_size)
	{
		return make_unexpected(secure_channel_errc::invalid_configuration);
	}

	const kind k = make_kind(t, false);
	auto ctx_result = pal::make_shared<context>(k);
	if (!ctx_result)
	{
		return pal::unexpected{ctx_result.error()};
	}

	auto &ctx = **ctx_result;

	std::ranges::copy(opts.certificate_chain, ctx.chain_refs.begin());
	ctx.chain_count = opts.certificate_chain.size();

	if (!ctx.alpn.encode(opts.supported_protocols))
	{
		return make_unexpected(secure_channel_errc::invalid_configuration);
	}

	if (!opts.use_system_trust)
	{
		ctx.root_store = make_cert_store(opts.trusted_roots);
		if (!ctx.root_store)
		{
			return make_unexpected(secure_channel_errc::invalid_configuration);
		}
	}

	// Manual validation: SChannel's own chain check is disabled and verify_peer() runs post-handshake
	SCHANNEL_CRED cred = {};
	cred.dwVersion = SCHANNEL_CRED_VERSION;
	cred.dwFlags = SCH_CRED_MANUAL_CRED_VALIDATION;
	if (ctx.root_store)
	{
		cred.hRootStore = ctx.root_store;
	}

	PCCERT_CONTEXT client_cert = nullptr;
	if (ctx.chain_count != 0)
	{
		client_cert = attorney::to_sys(ctx.chain_refs.front());
		if (!client_cert)
		{
			return make_unexpected(secure_channel_errc::invalid_configuration);
		}
		cred.cCreds = 1;
		cred.paCred = &client_cert;

		ctx.ca_store.add_intermediates(opts.certificate_chain.subspan(1));
	}
	else
	{
		cred.dwFlags |= SCH_CRED_NO_DEFAULT_CREDS;
	}

	const SECURITY_STATUS ss = ::AcquireCredentialsHandleW(
		nullptr,
		const_cast<SEC_WCHAR *>(UNISP_NAME_W),
		SECPKG_CRED_OUTBOUND,
		nullptr,
		&cred,
		nullptr,
		nullptr,
		&ctx.cred,
		nullptr
	);

	if (ss != SEC_E_OK)
	{
		SecInvalidateHandle(&ctx.cred);
		return make_unexpected(secure_channel_errc::invalid_configuration);
	}

	return *ctx_result;
}

result<handshake_channel> make_channel (const context_ptr &ctx, const acceptor_handshake_options &opts) noexcept //{{{1
{
	auto state = pal::make_unique<session_state>(ctx, is_datagram(ctx->kind), opts.relax, std::string_view{});
	if (!state)
	{
		return pal::unexpected{state.error()};
	}
	return attorney::emit_handshake_channel(std::move(*state));
}

result<handshake_channel> make_channel (const context_ptr &ctx, const connector_handshake_options &opts) noexcept //{{{1
{
	if (opts.peer_name.size() > session_state::max_peer_name)
	{
		return make_unexpected(secure_channel_errc::invalid_configuration);
	}

	auto state = pal::make_unique<session_state>(ctx, is_datagram(ctx->kind), opts.relax, opts.peer_name);
	if (!state)
	{
		return pal::unexpected{state.error()};
	}
	return attorney::emit_handshake_channel(std::move(*state));
}

//}}}1

} // namespace __secure_channel

SECURITY_STATUS session_state::do_step ( //{{{1
	std::span<const std::byte> in,
	sec_buffer_desc &in_desc,
	sec_buffer_desc &out_desc) noexcept
{
	DWORD ret_flags = 0;
	SECURITY_STATUS ss;
	const bool first = !SecIsValidHandle(&security_ctx);

	if (is_acceptor(config->kind))
	{
		DWORD req_flags = is_datagram ? asc_dtls_flags : asc_stream_flags;
		if (config->require_client_certificate)
		{
			req_flags |= ASC_REQ_MUTUAL_AUTH;
		}

		ss = ::AcceptSecurityContext(
			&config->cred,
			first ? nullptr : &security_ctx,
			&in_desc,
			req_flags,
			0,
			&security_ctx,
			&out_desc,
			&ret_flags,
			nullptr
		);
	}
	else
	{
		std::array<wchar_t, 256> target_name{};
		const wchar_t *p_target = to_wide(peer_name(), target_name);

		const DWORD req_flags = is_datagram ? isc_dtls_flags : isc_stream_flags;
		const bool pass_input = !first || !in.empty() || config->alpn.buffer_size > 0;

		ss = ::InitializeSecurityContextW(
			&config->cred,
			first ? nullptr : &security_ctx,
			const_cast<SEC_WCHAR *>(p_target),
			req_flags,
			0,
			0,
			pass_input ? &in_desc : nullptr,
			0,
			&security_ctx,
			&out_desc,
			&ret_flags,
			nullptr
		);
	}

	// On a *failed first* call SChannel may leave the security_ctx partially written,
	// so restore the invalid sentinel; otherwise it could read back as an established
	// context the dtor would wrongly free. A failed *continuation* call leaves the
	// already-established context intact and must NOT be invalidated (doing so would orphan it),
	// hence the first gate.
	if (first && ss != SEC_I_CONTINUE_NEEDED && ss != SEC_I_MESSAGE_FRAGMENT && ss != SEC_E_OK)
	{
		SecInvalidateHandle(&security_ctx);
	}

	return ss;
}

std::error_code session_state::verify_peer () noexcept //{{{1
{
	const bool acceptor = is_acceptor(config->kind);
	if (acceptor && !config->require_client_certificate)
	{
		return {};
	}

	PCCERT_CONTEXT raw = nullptr;
	const auto ss = ::QueryContextAttributesW(&security_ctx, SECPKG_ATTR_REMOTE_CERT_CONTEXT, &raw);
	if (ss != SEC_E_OK || raw == nullptr)
	{
		// clang-format off
		return make_error_code(acceptor
			? secure_channel_errc::client_certificate_required
			: secure_channel_errc::peer_verification_failed
		);
		// clang-format on
	}
	cert_ptr peer_cert{raw};

	if (acceptor)
	{
		if (config->root_store)
		{
			return config->verify_peer_cert(peer_cert, {}, verify_relax::none, AUTHTYPE_CLIENT);
		}
		return {};
	}

	return config->verify_peer_cert(peer_cert, peer_name(), relax, AUTHTYPE_SERVER);
}

result<handshake_result> handshake_channel::step_impl ( //{{{1
	std::span<const std::byte> in,
	std::span<std::byte> out) noexcept
{
	using namespace __secure_channel;

	if (!impl_)
	{
		return make_unexpected(secure_channel_errc::invalid_configuration);
	}
	auto &state = *impl_->state;
	auto &shared = *state.config;

	// Bogus constant DTLS cookie peer-address. TODO(dtls-cookie): replace with the caller's real peer
	// endpoint once the contract carries it; until then DTLS anti-amplification is forfeited.
	unsigned char dtls_cookie_addr[8] = {10, 0, 0, 1, 0, 0, 0, 1};

	sec_buffer in_bufs[4] = {
		{SECBUFFER_TOKEN, in},
		{SECBUFFER_EMPTY},
	};
	ULONG in_count = 2;

	if (shared.alpn.buffer_size > 0)
	{
		// clang-format off
		in_bufs[in_count++] = {
			SECBUFFER_APPLICATION_PROTOCOLS,
			shared.alpn.buffer.data(),
			shared.alpn.buffer_size
		};
		// clang-format on
	}

	// Cookie address on every server call: SChannel needs it to generate the HelloVerifyRequest cookie
	// (first call) and to verify the echoed cookie on ClientHello#2 (second call). Passing it only on
	// the first call breaks the handshake.
	if (shared.kind == kind::datagram_acceptor)
	{
		in_bufs[in_count++] = {SECBUFFER_EXTRA, dtls_cookie_addr, sizeof(dtls_cookie_addr)};
	}
	sec_buffer_desc in_desc{in_bufs, in_count};

	sec_buffer out_bufs[] = {
		{SECBUFFER_TOKEN, out},
		{SECBUFFER_EMPTY},
	};
	sec_buffer_desc out_desc{out_bufs};

	const SECURITY_STATUS ss = state.do_step(in, in_desc, out_desc);

	handshake_result r{};
	r.consumed = in.size();
	if (in_bufs[1].BufferType == SECBUFFER_EXTRA && in_bufs[1].cbBuffer > 0)
	{
		// SChannel left some input unconsumed (reported in the reserved EXTRA slot; the cookie EXTRA, when
		// present, lives in a later slot and must not be counted here).
		r.consumed -= in_bufs[1].cbBuffer;
	}
	r.produced = out_bufs[0].cbBuffer;

	switch (ss)
	{
		case SEC_E_INCOMPLETE_MESSAGE:
			// SChannel writes nothing on INCOMPLETE_MESSAGE (cbBuffer stays at initial capacity)
			r.want_input = true;
			r.consumed = 0;
			r.produced = 0;
			return r;

		case SEC_I_MESSAGE_FRAGMENT:
			// DTLS flight fragmented across records: emit this fragment and ask to be called again with a
			// fresh output buffer. Input stays unconsumed; SChannel emits the next fragment from it.
			r.want_output = true;
			r.consumed = 0;
			return r;

		case SEC_I_CONTINUE_NEEDED:
			r.want_input = true;
			return r;

		case SEC_E_BUFFER_TOO_SMALL:
			r.want_output = true;
			r.consumed = 0;
			r.produced = 0;
			return r;

		case SEC_E_OK:
			break;

		default:
			return pal::unexpected{map_handshake_error(ss)};
	}

	if (auto ec = state.verify_peer())
	{
		return pal::unexpected{ec};
	}

	auto conn = attorney::emit_connected_channel(std::move(impl_->state));
	if (!conn)
	{
		return pal::unexpected{conn.error()};
	}
	impl_.reset();

	r.connected.emplace(std::move(*conn));
	return r;
}

result<channel_result> connected_channel::encrypt_impl ( //{{{1
	std::span<const std::byte> plain,
	std::span<std::byte> out) noexcept
{
	if (!impl_)
	{
		return make_unexpected(secure_channel_errc::invalid_configuration);
	}
	if (impl_->closed)
	{
		return make_unexpected(secure_channel_errc::closed);
	}

	channel_result r{};
	if (plain.empty())
	{
		return r;
	}

	auto &state = *impl_->state;
	const auto &sizes = impl_->sizes;

	if (state.is_datagram && plain.size() > max_message_size())
	{
		return make_unexpected(secure_channel_errc::message_too_large);
	}

	const auto rec_size = state.is_datagram ? plain.size() : std::min<size_t>(plain.size(), sizes.cbMaximumMessage);
	if (out.size() < sizes.cbHeader + rec_size + sizes.cbTrailer)
	{
		r.want_output = true;
		return r;
	}

	std::memcpy(out.data() + sizes.cbHeader, plain.data(), rec_size);
	sec_buffer enc_bufs[] = {
		{SECBUFFER_STREAM_HEADER, out.first(sizes.cbHeader)},
		{SECBUFFER_DATA, out.subspan(sizes.cbHeader, rec_size)},
		{SECBUFFER_STREAM_TRAILER, out.subspan(sizes.cbHeader + rec_size, sizes.cbTrailer)},
		{SECBUFFER_EMPTY},
	};
	sec_buffer_desc enc_desc{enc_bufs};

	if (::EncryptMessage(&state.security_ctx, 0, &enc_desc, 0) != SEC_E_OK)
	{
		return make_unexpected(secure_channel_errc::protocol_error);
	}

	// Use enc_bufs[2].cbBuffer (actual trailer bytes written) not sizes.cbTrailer (allocated
	// space). For datagram contexts, EncryptMessage allocates cbTrailer bytes but only writes
	// a smaller AEAD tag; the remainder is SChannel-internal padding that must not be included
	// in produced so that cipher[produced-1] is the last byte of the AEAD tag.
	r.consumed = rec_size;
	r.produced = sizes.cbHeader + rec_size + enc_bufs[2].cbBuffer;
	r.want_output = (rec_size < plain.size());
	return r;
}

result<channel_result> connected_channel::decrypt_impl ( //{{{1
	std::span<const std::byte> cipher,
	std::span<std::byte> out) noexcept
{
	if (!impl_)
	{
		return make_unexpected(secure_channel_errc::invalid_configuration);
	}

	auto &impl = *impl_;
	auto &state = *impl.state;

	channel_result r{};

	// Drain the undelivered plaintext tail from a previous oversized record first. It still lives in
	// cipher_buf (DecryptMessage decrypted in place); this block returns before any new ciphertext is
	// accumulated, so the tail can never be clobbered while pending. consumed stays 0, so the caller
	// re-presents the same ciphertext on the next call.
	if (impl.plain_first < impl.plain_last)
	{
		const size_t avail = impl.plain_last - impl.plain_first;
		const size_t to_copy = std::min(avail, out.size());
		if (to_copy > 0)
		{
			std::memcpy(out.data(), impl.cipher_buf.data() + impl.plain_first, to_copy);
			impl.plain_first += to_copy;
			if (impl.plain_first == impl.plain_last)
			{
				impl.plain_first = impl.plain_last = 0;
			}
		}
		r.produced = to_copy;
		r.want_output = (impl.plain_first < impl.plain_last);
		return r;
	}

	if (cipher.empty())
	{
		r.want_input = true;
		return r;
	}

	// Accumulate inbound ciphertext in cipher_buf and decrypt in place. A whole datagram (or stream
	// segment) may hold several records; SECBUFFER_EXTRA marks the next record's bytes.
	const size_t space = impl.cipher_buf.size() - impl.cipher_pending;
	const size_t new_bytes = std::min(cipher.size(), space);
	std::memcpy(impl.cipher_buf.data() + impl.cipher_pending, cipher.data(), new_bytes);
	const size_t total = impl.cipher_pending + new_bytes;

	// One DATA buffer in; the three spare slots default to SECBUFFER_EMPTY for DecryptMessage to fill
	// with the plaintext DATA and any trailing SECBUFFER_EXTRA (next record).
	sec_buffer dec_bufs[4] = {
		{SECBUFFER_DATA, std::span{impl.cipher_buf}.first(total)},
	};
	sec_buffer_desc dec_desc{dec_bufs};

	const SECURITY_STATUS ss = ::DecryptMessage(&state.security_ctx, &dec_desc, 0, nullptr);

	if (ss == SEC_E_INCOMPLETE_MESSAGE)
	{
		// The combined bytes don't form a complete record yet; retain them for the next call. If the buffer
		// is already full the record is larger than record_cap (oversized or hostile framing): it can never
		// complete, so fail instead of looping forever on a buffer that cannot grow.
		if (total == impl.cipher_buf.size())
		{
			return make_unexpected(secure_channel_errc::protocol_error);
		}
		impl.cipher_pending = total;
		r.want_input = true;
		r.consumed = new_bytes;
		return r;
	}

	// Record processed (success or error): clear accumulation state.
	impl.cipher_pending = 0;
	r.consumed = new_bytes;

	if (ss == SEC_I_CONTEXT_EXPIRED)
	{
		r.peer_closed = true;
		return r;
	}

	if (ss == SEC_I_RENEGOTIATE)
	{
		// Renegotiation is unsupported: fail rather than attempt an in-stream re-handshake (the data channel
		// has no handshake path). The OpenSSL backend sets SSL_OP_NO_RENEGOTIATION for the same policy.
		return make_unexpected(secure_channel_errc::protocol_error);
	}

	if (ss == SEC_E_DECRYPT_FAILURE || ss == SEC_E_MESSAGE_ALTERED || ss == SEC_E_INVALID_TOKEN)
	{
		if (state.is_datagram)
		{
			// DTLS silently discards records that fail authentication (RFC 6347): drop this record,
			// produce nothing, surface no error and no peer_closed. The context survives, so the next
			// valid record decrypts normally. (This is the inverse of the stream path, and closes the
			// truncation/DoS hole — a forged datagram can no longer tear the connection down.)
			return r;
		}
		return make_unexpected(secure_channel_errc::decrypt_failed);
	}

	if (ss != SEC_E_OK)
	{
		return make_unexpected(secure_channel_errc::protocol_error);
	}

	// Find SECBUFFER_DATA (plaintext) and SECBUFFER_EXTRA (unconsumed ciphertext within total).
	std::byte *plain_ptr = nullptr;
	size_t plain_size = 0;
	size_t extra_size = 0;

	for (const auto &buf: dec_bufs)
	{
		if (buf.BufferType == SECBUFFER_DATA && buf.pvBuffer)
		{
			plain_ptr = static_cast<std::byte *>(buf.pvBuffer);
			plain_size = buf.cbBuffer;
		}
		else if (buf.BufferType == SECBUFFER_EXTRA)
		{
			extra_size = buf.cbBuffer;
		}
	}

	// Consumed = bytes accumulated this call minus the unconsumed tail (SECBUFFER_EXTRA, the next record).
	// The min() guards a size_t underflow should extra ever exceed new_bytes.
	r.consumed = new_bytes - std::min(extra_size, new_bytes);

	if (plain_size == 0)
	{
		return r;
	}

	// Copy as much plaintext as fits in out; the undelivered tail stays in cipher_buf for drain calls.
	const size_t to_out = std::min(plain_size, out.size());
	if (to_out > 0)
	{
		std::memcpy(out.data(), plain_ptr, to_out);
	}
	r.produced = to_out;

	if (to_out < plain_size)
	{
		// plain_ptr points into cipher_buf, so record the undelivered tail as offsets into it rather than
		// copying it out. The drain block above serves it on later calls and returns before cipher_buf is
		// reused, so it survives untouched until fully drained.
		const size_t plain_off = static_cast<size_t>(plain_ptr - impl.cipher_buf.data());
		impl.plain_first = plain_off + to_out;
		impl.plain_last = plain_off + plain_size;
		r.want_output = true;
	}

	return r;
}

result<channel_result> connected_channel::close_impl (std::span<std::byte> out) noexcept //{{{1
{
	if (!impl_)
	{
		return make_unexpected(secure_channel_errc::invalid_configuration);
	}

	auto &impl = *impl_;
	auto &state = *impl.state;

	channel_result r{};
	if (impl.close_done)
	{
		return r;
	}

	// Datagram: SChannel cannot generate a DTLS close_notify (ApplyControlToken + ISC/ASC renegotiates
	// rather than shutting down). Close is a best-effort local no-op — mark the channel closed so
	// encrypt() returns `closed`; the peer detects closure via the transport. The receive path still
	// reports peer_closed if a peer that does emit close_notify (e.g. OpenSSL) sends one.
	if (state.is_datagram)
	{
		impl.closed = true;
		impl.close_done = true;
		return r;
	}

	if (!impl.closed)
	{
		DWORD shutdown_token = SCHANNEL_SHUTDOWN;
		sec_buffer token_buf{SECBUFFER_TOKEN, &shutdown_token, sizeof(shutdown_token)};
		sec_buffer_desc token_desc{&token_buf, 1};
		::ApplyControlToken(&state.security_ctx, &token_desc);
		impl.closed = true;
	}

	sec_buffer out_buf{SECBUFFER_TOKEN, out};
	sec_buffer_desc out_desc{&out_buf, 1};
	sec_buffer in_buf{SECBUFFER_EMPTY};
	sec_buffer_desc in_desc{&in_buf, 1};

	DWORD ret_flags = 0;
	SECURITY_STATUS ss;

	if (is_acceptor(state.config->kind))
	{
		ss = ::AcceptSecurityContext(
			&state.config->cred,
			&state.security_ctx,
			&in_desc,
			asc_stream_flags,
			SECURITY_NATIVE_DREP,
			nullptr,
			&out_desc,
			&ret_flags,
			nullptr
		);
	}
	else
	{
		std::array<wchar_t, 256> target_name{};
		const wchar_t *p_target = to_wide(state.peer_name(), target_name);
		ss = ::InitializeSecurityContextW(
			&state.config->cred,
			&state.security_ctx,
			const_cast<SEC_WCHAR *>(p_target),
			isc_stream_flags,
			0,
			SECURITY_NATIVE_DREP,
			&in_desc,
			0,
			nullptr,
			&out_desc,
			&ret_flags,
			nullptr
		);
	}

	r.produced = out_buf.cbBuffer;

	if (ss == SEC_E_OK || ss == SEC_I_CONTEXT_EXPIRED)
	{
		impl.close_done = true;
	}
	else if (ss == SEC_E_BUFFER_TOO_SMALL)
	{
		r.want_output = true;
	}

	return r;
}

result<certificate> connected_channel::peer_certificate () const noexcept //{{{1
{
	if (!impl_)
	{
		return certificate{};
	}

	PCCERT_CONTEXT raw = nullptr;
	const auto ss = ::QueryContextAttributesW(&impl_->state->security_ctx, SECPKG_ATTR_REMOTE_CERT_CONTEXT, &raw);
	if (ss != SEC_E_OK || !raw)
	{
		return certificate{};
	}

	const cert_ptr cert{raw};
	return certificate::from_der(std::span{cert->pbCertEncoded, cert->cbCertEncoded});
}

std::string_view connected_channel::selected_protocol () const noexcept //{{{1
{
	if (!impl_)
	{
		return {};
	}
	return impl_->alpn.view;
}

size_t connected_channel::max_message_size () const noexcept //{{{1
{
	if (!impl_)
	{
		return 0;
	}
	if (impl_->state->is_datagram)
	{
		const auto &s = impl_->sizes;
		const size_t overhead = static_cast<size_t>(s.cbHeader) + s.cbTrailer;
		return s.cbMaximumMessage > overhead ? s.cbMaximumMessage - overhead : 0;
	}
	return SIZE_MAX;
}

//}}}1

} // namespace pal::crypto

#endif // __pal_crypto_windows
