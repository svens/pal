#include <pal/crypto/__crypto.hpp>

#if __pal_crypto_openssl

#include <pal/crypto/__certificate.hpp>
#include <pal/crypto/__secure_channel.hpp>
#include <pal/crypto/secure_channel.hpp>
#include <pal/memory.hpp>
#include <openssl/bio.h>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <mutex>
#include <optional>
#include <utility>

namespace pal::crypto
{

namespace
{

// IPv4 (20) + UDP (8); pick a safe constant for MTU overhead reporting.
constexpr long dgram_mtu_overhead = 28;
constexpr int dtls_mtu = 1500 - dgram_mtu_overhead;

using __secure_channel::kind;

const SSL_METHOD *method_for (kind k) noexcept //{{{1
{
	switch (k)
	{
		case kind::stream_acceptor:
			return ::TLS_server_method();
		case kind::stream_connector:
			return ::TLS_client_method();
		case kind::datagram_acceptor:
			return ::DTLS_server_method();
		case kind::datagram_connector:
			return ::DTLS_client_method();
	}
	return nullptr;
}

struct bio_io //{{{1
{
	std::span<const std::byte> read_in;
	size_t read_consumed = 0;
	std::span<std::byte> write_out;
	size_t write_produced = 0;
	bool write_overflow = false;
};

struct bio_method //{{{1
{
	static inline ::BIO_METHOD *methods = nullptr;

	static int create (::BIO *bio) noexcept
	{
		::BIO_set_init(bio, 1);
		return 1;
	}

	static long ctrl (::BIO *, int cmd, long, void *) noexcept
	{
		switch (cmd)
		{
			case BIO_CTRL_PENDING:
			case BIO_CTRL_WPENDING:
			case BIO_CTRL_DGRAM_MTU_EXCEEDED:
				return 0;
			case BIO_CTRL_DGRAM_GET_MTU_OVERHEAD:
				return dgram_mtu_overhead;
	#if defined(BIO_CTRL_GET_KTLS_SEND) && defined(BIO_CTRL_GET_KTLS_RECV)
			case BIO_CTRL_GET_KTLS_SEND:
			case BIO_CTRL_GET_KTLS_RECV:
				return 0;
	#endif
			default:
				break;
		}
		return 1;
	}

	static int write (::BIO *bio, const char *data, int size) noexcept
	{
		::BIO_clear_retry_flags(bio);

		auto *io = static_cast<bio_io *>(::BIO_get_data(bio));
		if (io == nullptr)
		{
			return -1;
		}

		const auto remaining = static_cast<int>(io->write_out.size() - io->write_produced);
		const auto chunk = std::min(remaining, size);
		if (chunk == 0)
		{
			io->write_overflow = true;
			::BIO_set_retry_write(bio);
			return -1;
		}

		std::memcpy(io->write_out.data() + io->write_produced, data, chunk);
		io->write_produced += chunk;
		if (chunk < size)
		{
			io->write_overflow = true;
		}

		return chunk;
	}

	static int read (::BIO *bio, char *data, int size) noexcept
	{
		::BIO_clear_retry_flags(bio);

		auto *io = static_cast<bio_io *>(::BIO_get_data(bio));
		if (io == nullptr)
		{
			return -1;
		}

		const auto remaining = static_cast<int>(io->read_in.size() - io->read_consumed);
		if (remaining == 0)
		{
			::BIO_set_retry_read(bio);
			return -1;
		}

		const auto chunk = std::min(remaining, size);
		std::memcpy(data, io->read_in.data() + io->read_consumed, chunk);
		io->read_consumed += chunk;

		return chunk;
	}

	static void init () noexcept
	{
		methods = ::BIO_meth_new(BIO_TYPE_SOURCE_SINK | ::BIO_get_new_index(), "pal::crypto::secure_channel");
		if (methods != nullptr)
		{
			::BIO_meth_set_create(methods, &bio_method::create);
			::BIO_meth_set_ctrl(methods, &bio_method::ctrl);
			::BIO_meth_set_write(methods, &bio_method::write);
			::BIO_meth_set_read(methods, &bio_method::read);
		}
	}

	static ::BIO_METHOD *get () noexcept
	{
		static std::once_flag flag;
		std::call_once(flag, &bio_method::init);
		return methods;
	}
};

int session_index () noexcept //{{{1
{
	static const auto idx = ::SSL_get_ex_new_index(0, nullptr, nullptr, nullptr, nullptr);
	return idx;
}

struct ssl_ctx_deleter //{{{1
{
	void operator() (::SSL_CTX *p) const noexcept
	{
		::SSL_CTX_free(p);
	}
};
using ssl_ctx_ptr = std::unique_ptr<::SSL_CTX, ssl_ctx_deleter>;

struct ssl_deleter //{{{1
{
	void operator() (::SSL *p) const noexcept
	{
		::SSL_free(p);
	}
};
using ssl_ptr = std::unique_ptr<::SSL, ssl_deleter>;

// DTLS HelloVerifyRequest cookie HMAC key size (a 256-bit key).
constexpr size_t cookie_secret_size = 32;
using cookie_secret_type = std::array<unsigned char, cookie_secret_size>;

// session_state {{{1

/// Shared session state held by handshake_channel::impl_type and migrated to
/// connected_channel::impl_type on handshake completion.
struct session_state
{
	__secure_channel::context_ptr ctx;
	ssl_ptr ssl;
	::BIO *bio = nullptr; // owned by SSL via SSL_set_bio
	verify_relax relax = verify_relax::none;
	bool is_datagram = false;
	bool closed = false;
	bool peer_closed = false;

	// Slot for peer cert verification result, used by verify callback.
	int verify_error = 0;

	// Per-handshake snapshot of the acceptor's cookie HMAC key (copied at accept time)
	cookie_secret_type cookie_secret{};

	// DTLS anti-amplification cookie binding bytes (datagram acceptor only); empty disables the exchange.
	static constexpr size_t max_peer_token = 64;
	std::array<unsigned char, max_peer_token> peer_token_buf{};
	size_t peer_token_size = 0;

	void peer_token (std::span<const std::byte> token) noexcept
	{
		peer_token_size = token.size();
		if (!token.empty())
		{
			std::memcpy(peer_token_buf.data(), token.data(), token.size());
		}
	}

	static int verify_callback (int preverify_ok, ::X509_STORE_CTX *store_ctx) noexcept;
};
using session_state_ptr = std::unique_ptr<session_state>;

int session_state::verify_callback (int preverify_ok, ::X509_STORE_CTX *store_ctx) noexcept
{
	static const auto ssl_index = ::SSL_get_ex_data_X509_STORE_CTX_idx();
	auto *ssl = static_cast<::SSL *>(::X509_STORE_CTX_get_ex_data(store_ctx, ssl_index));
	if (ssl == nullptr)
	{
		return preverify_ok;
	}

	auto *state = static_cast<session_state *>(::SSL_get_ex_data(ssl, session_index()));
	if (state == nullptr)
	{
		return preverify_ok;
	}

	if (preverify_ok == 1)
	{
		if (::X509_STORE_CTX_get_error_depth(store_ctx) == 0)
		{
			state->verify_error = X509_V_OK;
		}
		return 1;
	}

	const int err = ::X509_STORE_CTX_get_error(store_ctx);
	if (err == X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT && any(state->relax & verify_relax::self_signed))
	{
		state->verify_error = X509_V_OK;
		return 1;
	}

	state->verify_error = err;
	return 0;
}

// Map a failed handshake to an errc and consume (clear) the OpenSSL error queue.
std::error_code map_openssl_error (const session_state &state) noexcept //{{{1
{
	auto ec = make_error_code(secure_channel_errc::handshake_failed);

	if (state.verify_error != 0 && state.verify_error != X509_V_OK)
	{
		ec = state.verify_error == X509_V_ERR_HOSTNAME_MISMATCH
			? make_error_code(secure_channel_errc::peer_hostname_mismatch)
			: make_error_code(secure_channel_errc::peer_verification_failed);
	}
	else if (const auto err = ::ERR_peek_last_error(); err != 0)
	{
		switch (ERR_GET_REASON(err))
		{
			case SSL_R_NO_APPLICATION_PROTOCOL:
			case SSL_R_TLSV1_ALERT_NO_APPLICATION_PROTOCOL:
				ec = make_error_code(secure_channel_errc::no_application_protocol);
				break;
			case SSL_R_PEER_DID_NOT_RETURN_A_CERTIFICATE:
			case SSL_R_TLSV13_ALERT_CERTIFICATE_REQUIRED:
				ec = make_error_code(secure_channel_errc::client_certificate_required);
				break;
			case SSL_R_CERTIFICATE_VERIFY_FAILED:
				ec = make_error_code(secure_channel_errc::peer_verification_failed);
				break;
			default:
				break;
		}
	}

	::ERR_clear_error();
	return ec;
}

// Map a post-handshake I/O failure to an errc and consume (clear) the OpenSSL error queue.
std::error_code map_openssl_io_error (session_state &state, int ssl_error) noexcept //{{{1
{
	std::error_code ec;
	if (ssl_error == SSL_ERROR_ZERO_RETURN)
	{
		state.peer_closed = true;
	}
	// A tampered or corrupt inbound record fails authenticated decryption. On the stream path OpenSSL
	// reports a record-layer failure wrapping SSL_R_DECRYPTION_FAILED_OR_BAD_RECORD_MAC, which is the
	// root cause at the bottom of the error queue (peek_error, not peek_last).
	else if (ERR_GET_REASON(::ERR_peek_error()) == SSL_R_DECRYPTION_FAILED_OR_BAD_RECORD_MAC)
	{
		ec = make_error_code(secure_channel_errc::decrypt_failed);
	}
	else
	{
		ec = make_error_code(secure_channel_errc::protocol_error);
	}

	::ERR_clear_error();
	return ec;
}

result<ssl_ctx_ptr> make_ssl_ctx (kind k) noexcept //{{{1
{
	const auto *method = method_for(k);
	if (method == nullptr)
	{
		return make_unexpected(secure_channel_errc::invalid_configuration);
	}

	ssl_ctx_ptr ctx{::SSL_CTX_new(method)};
	if (!ctx)
	{
		return make_unexpected(std::errc::not_enough_memory);
	}

	::SSL_CTX_set_read_ahead(ctx.get(), 0);

	if (!is_datagram(k))
	{
		::SSL_CTX_set_mode(ctx.get(), SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
	}

	::SSL_CTX_set_options(ctx.get(), SSL_OP_NO_RENEGOTIATION);

	return ctx;
}

//}}}1

} // namespace

struct handshake_channel::impl_type //{{{1
{
	session_state_ptr state;

	explicit impl_type (session_state_ptr s) noexcept
		: state{std::move(s)}
	{
	}
};

handshake_channel::~handshake_channel () noexcept = default;
handshake_channel::handshake_channel () = default;
handshake_channel::handshake_channel (handshake_channel &&) noexcept = default;
handshake_channel &handshake_channel::operator= (handshake_channel &&) noexcept = default;

handshake_channel::handshake_channel (impl_ptr impl) noexcept
	: impl_{std::move(impl)}
{
}

struct connected_channel::impl_type //{{{1
{
	session_state_ptr state;

	explicit impl_type (session_state_ptr s) noexcept
		: state{std::move(s)}
	{
	}
};

connected_channel::~connected_channel () noexcept = default;
connected_channel::connected_channel () = default;
connected_channel::connected_channel (connected_channel &&) noexcept = default;
connected_channel &connected_channel::operator= (connected_channel &&) noexcept = default;

connected_channel::connected_channel (impl_ptr impl) noexcept
	: impl_{std::move(impl)}
{
}

//}}}1

namespace __secure_channel
{

struct context //{{{1
{
	const enum kind kind;
	ssl_ctx_ptr ssl_ctx;

	// DTLS HelloVerifyRequest cookie HMAC master key; randomized for datagram_acceptor (unused for other kinds)
	cookie_secret_type cookie_secret{};

	explicit context (enum kind kind) noexcept
		: kind{kind}
	{
	}

	struct alpn
	{
		static constexpr size_t max_wire_size = 256;
		std::array<char, max_wire_size> storage;
		std::string_view wire;

		/// Encode \a protocols into storage and update wire view.
		/// Returns false on empty/oversized protocol name or buffer overflow.
		[[nodiscard]] bool encode (std::span<const std::string_view> protocols) noexcept;

		/// Pick first server-preferred protocol that also appears in \a client_wire.
		/// Returns the chosen protocol (view into storage) or empty if no match.
		[[nodiscard]] std::string_view select (std::string_view client_wire) const noexcept;
	} alpn{};
};

bool context::alpn::encode (std::span<const std::string_view> protocols) noexcept
{
	if (const auto n = encode_alpn_wire(protocols, std::as_writable_bytes(std::span{storage})))
	{
		wire = std::string_view{storage.data(), *n};
		return true;
	}
	return false;
}

std::string_view context::alpn::select (std::string_view client_wire) const noexcept
{
	static constexpr auto contains = [] (std::string_view list, std::string_view proto) noexcept
	{
		while (!list.empty())
		{
			const auto len = static_cast<uint8_t>(list[0]);
			list.remove_prefix(1);

			if (len > list.size())
			{
				return false;
			}

			if (const std::string_view current(list.begin(), list.begin() + len); current == proto)
			{
				return true;
			}

			list.remove_prefix(len);
		}

		return false;
	};

	auto server = wire;
	while (!server.empty())
	{
		const auto len = static_cast<uint8_t>(server[0]);
		server.remove_prefix(1);

		if (len > server.size())
		{
			break;
		}

		if (const std::string_view proto(server.begin(), server.begin() + len); contains(client_wire, proto))
		{
			return proto;
		}
		server.remove_prefix(len);
	}

	return {};
}

struct attorney //{{{1
{
	static auto to_sys (const certificate &c) noexcept
	{
		return c.impl_ ? c.impl_->x509.get() : nullptr;
	}

	static auto to_sys (const key &k) noexcept
	{
		return k.impl_ ? &k.impl_->pkey : nullptr;
	}

	// clang-format off

	static result<certificate> from_sys (::X509 *x509) noexcept
	{
		if (x509 == nullptr)
		{
			return certificate{};
		}

		return pal::make_shared<certificate::impl_type>(cert_ptr{x509}).transform([] (auto impl)
		{
			return certificate::to_api(std::move(impl));
		});
	}

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

//}}}1

namespace
{

bool apply_cert_chain (::SSL_CTX *ctx, std::span<const certificate> chain, const key &pkey) noexcept //{{{1
{
	if (chain.empty())
	{
		return true;
	}

	if (auto *leaf = attorney::to_sys(chain[0]); leaf == nullptr || ::SSL_CTX_use_certificate(ctx, leaf) != 1)
	{
		return false;
	}

	for (const auto &cert: chain.subspan(1))
	{
		if (auto *x509 = attorney::to_sys(cert); x509 == nullptr || ::SSL_CTX_add1_chain_cert(ctx, x509) != 1)
		{
			return false;
		}
	}

	auto *pk = attorney::to_sys(pkey);
	if (pk == nullptr)
	{
		return false;
	}
	if (::SSL_CTX_use_PrivateKey(ctx, pk) != 1)
	{
		return false;
	}
	if (::SSL_CTX_check_private_key(ctx) != 1)
	{
		return false;
	}

	return true;
}

bool apply_trust (::SSL_CTX *ctx, std::span<const certificate> roots, bool use_system) noexcept //{{{1
{
	if (use_system)
	{
		if (::SSL_CTX_set_default_verify_paths(ctx) != 1)
		{
			return false;
		}
	}

	if (roots.empty())
	{
		return true;
	}

	// clang-format off
	::X509_STORE *store = ::SSL_CTX_get_cert_store(ctx);
	return std::ranges::all_of(roots, [store] (const certificate &root) noexcept
	{
		if (auto *x509 = attorney::to_sys(root); x509 == nullptr)
		{
			return false;
		}
		else if (::X509_STORE_add_cert(store, x509) != 1)
		{
			const auto err = ::ERR_peek_last_error();
			::ERR_clear_error();

			// duplicates are fine, trust chain can be loaded multiple sources with overlapping issuers
			if (ERR_GET_REASON(err) != X509_R_CERT_ALREADY_IN_HASH_TABLE)
			{
				return false;
			}
		}
		return true;
	});
	// clang-format on
}

int alpn_select_callback ( //{{{1
	::SSL *,
	const unsigned char **out,
	unsigned char *outlen,
	const unsigned char *in,
	unsigned int inlen,
	void *arg) noexcept
{
	const auto *ctx = static_cast<const context *>(arg);
	const std::string_view client{reinterpret_cast<const char *>(in), inlen};

	const auto chosen = ctx->alpn.select(client);
	if (chosen.empty())
	{
		return SSL_TLSEXT_ERR_ALERT_FATAL;
	}

	*out = reinterpret_cast<const unsigned char *>(chosen.data());
	*outlen = static_cast<unsigned char>(chosen.size());
	return SSL_TLSEXT_ERR_OK;
}

bool compute_cookie (const session_state &state, unsigned char *out, unsigned int *out_len) noexcept //{{{1
{
	const auto &secret = state.cookie_secret;

	// clang-format off
	auto *result = ::HMAC(::EVP_sha256(),
		secret.data(),
		secret.size(),
		state.peer_token_buf.data(),
		state.peer_token_size,
		out,
		out_len
	);
	// clang-format on

	return result != nullptr;
}

int cookie_generate (::SSL *ssl, unsigned char *cookie, unsigned int *cookie_len) noexcept //{{{1
{
	const auto *state = static_cast<const session_state *>(::SSL_get_ex_data(ssl, session_index()));
	if (state == nullptr)
	{
		return 0;
	}
	return compute_cookie(*state, cookie, cookie_len) ? 1 : 0;
}

int cookie_verify (::SSL *ssl, const unsigned char *cookie, unsigned int cookie_len) noexcept //{{{1
{
	const auto *state = static_cast<const session_state *>(::SSL_get_ex_data(ssl, session_index()));
	if (state == nullptr)
	{
		return 0;
	}

	std::array<unsigned char, EVP_MAX_MD_SIZE> expected{};
	unsigned int expected_len = 0;
	if (!compute_cookie(*state, expected.data(), &expected_len))
	{
		return 0;
	}

	return cookie_len == expected_len && ::CRYPTO_memcmp(cookie, expected.data(), expected_len) == 0 ? 1 : 0;
}

result<session_state_ptr> make_session (const context_ptr &ctx) noexcept //{{{1
{
	// Precondition: ctx is a valid factory context. Factories carry no null state (no default ctor), so a
	// null here would be a use-after-move bug, not a runtime condition — deref rather than guard for it.

	auto state_result = pal::make_unique<session_state>();
	if (!state_result)
	{
		return pal::unexpected{state_result.error()};
	}

	auto state = std::move(*state_result);
	state->ctx = ctx;
	state->is_datagram = is_datagram(ctx->kind);

	ssl_ptr ssl{::SSL_new(ctx->ssl_ctx.get())};
	if (!ssl)
	{
		return make_unexpected(std::errc::not_enough_memory);
	}

	if (is_acceptor(ctx->kind))
	{
		::SSL_set_accept_state(ssl.get());
	}
	else
	{
		::SSL_set_connect_state(ssl.get());
	}

	auto *bio_methods = bio_method::get();
	if (bio_methods == nullptr)
	{
		return make_unexpected(std::errc::not_enough_memory);
	}

	auto *bio = ::BIO_new(bio_methods);
	if (bio == nullptr)
	{
		return make_unexpected(std::errc::not_enough_memory);
	}

	::SSL_set_bio(ssl.get(), bio, bio);
	state->bio = bio;

	if (state->is_datagram)
	{
		::SSL_set_options(ssl.get(), SSL_OP_NO_QUERY_MTU);
		::SSL_set_mtu(ssl.get(), dtls_mtu);
	}

	::SSL_set_ex_data(ssl.get(), session_index(), state.get());

	state->ssl = std::move(ssl);
	return state;
}

result<context_ptr>
wrap_context (kind k, ssl_ctx_ptr ssl_ctx, std::span<const std::string_view> protocols) noexcept //{{{1
{
	auto ctx = pal::make_shared<context>(k);
	if (!ctx)
	{
		return pal::unexpected{ctx.error()};
	}

	auto &c = **ctx;
	c.ssl_ctx = std::move(ssl_ctx);

	if (!c.alpn.encode(protocols))
	{
		return make_unexpected(secure_channel_errc::invalid_configuration);
	}

	if (!c.alpn.wire.empty())
	{
		if (is_acceptor(k))
		{
			// server picks from the client's list (server preference)
			::SSL_CTX_set_alpn_select_cb(c.ssl_ctx.get(), &alpn_select_callback, &c);
		}
		else
		{
			// client advertises its list
			const int ret = ::SSL_CTX_set_alpn_protos(
				c.ssl_ctx.get(),
				reinterpret_cast<const unsigned char *>(c.alpn.wire.data()),
				static_cast<unsigned int>(c.alpn.wire.size())
			);
			if (ret != 0)
			{
				::ERR_clear_error();
				return make_unexpected(secure_channel_errc::invalid_configuration);
			}
		}
	}

	if (k == kind::datagram_acceptor)
	{
		if (::RAND_bytes(c.cookie_secret.data(), c.cookie_secret.size()) != 1)
		{
			::ERR_clear_error();
			return make_unexpected(secure_channel_errc::invalid_configuration);
		}
		::SSL_CTX_set_cookie_generate_cb(c.ssl_ctx.get(), &cookie_generate);
		::SSL_CTX_set_cookie_verify_cb(c.ssl_ctx.get(), &cookie_verify);
	}

	return *ctx;
}

//}}}1

} // namespace

result<context_ptr> make_context (transport t, const acceptor_options &opts) noexcept //{{{1
{
	if (opts.certificate_chain.empty() || attorney::to_sys(opts.private_key) == nullptr)
	{
		return make_unexpected(secure_channel_errc::invalid_configuration);
	}

	const kind k = make_kind(t, true);
	auto ctx_result = make_ssl_ctx(k);
	if (!ctx_result)
	{
		return pal::unexpected{ctx_result.error()};
	}

	auto ssl_ctx = std::move(*ctx_result);
	::SSL_CTX_set_options(ssl_ctx.get(), SSL_OP_NO_SESSION_RESUMPTION_ON_RENEGOTIATION);
	::SSL_CTX_set_num_tickets(ssl_ctx.get(), 0);

	if (!apply_cert_chain(ssl_ctx.get(), opts.certificate_chain, opts.private_key))
	{
		::ERR_clear_error();
		return make_unexpected(secure_channel_errc::invalid_configuration);
	}

	int verify_mode = SSL_VERIFY_NONE;
	if (opts.require_client_certificate)
	{
		verify_mode = SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT;
		if (!apply_trust(ssl_ctx.get(), opts.trusted_roots, opts.use_system_trust))
		{
			::ERR_clear_error();
			return make_unexpected(secure_channel_errc::invalid_configuration);
		}
	}
	::SSL_CTX_set_verify(ssl_ctx.get(), verify_mode, &session_state::verify_callback);

	return wrap_context(k, std::move(ssl_ctx), opts.supported_protocols);
}

result<context_ptr> make_context (transport t, const connector_options &opts) noexcept //{{{1
{
	if (!opts.certificate_chain.empty() && attorney::to_sys(opts.private_key) == nullptr)
	{
		return make_unexpected(secure_channel_errc::invalid_configuration);
	}

	const kind k = make_kind(t, false);
	auto ctx_result = make_ssl_ctx(k);
	if (!ctx_result)
	{
		return pal::unexpected{ctx_result.error()};
	}

	auto ssl_ctx = std::move(*ctx_result);
	if (!apply_trust(ssl_ctx.get(), opts.trusted_roots, opts.use_system_trust))
	{
		::ERR_clear_error();
		return make_unexpected(secure_channel_errc::invalid_configuration);
	}

	if (!opts.certificate_chain.empty())
	{
		if (!apply_cert_chain(ssl_ctx.get(), opts.certificate_chain, opts.private_key))
		{
			::ERR_clear_error();
			return make_unexpected(secure_channel_errc::invalid_configuration);
		}
	}

	::SSL_CTX_set_verify(ssl_ctx.get(), SSL_VERIFY_PEER, &session_state::verify_callback);

	return wrap_context(k, std::move(ssl_ctx), opts.supported_protocols);
}

result<handshake_channel> make_channel ( //{{{1
	const context_ptr &ctx,
	const acceptor_handshake_options &opts,
	std::span<const std::byte> peer_token) noexcept
{
	if (peer_token.size() > session_state::max_peer_token)
	{
		return make_unexpected(secure_channel_errc::invalid_configuration);
	}

	auto state_result = make_session(ctx);
	if (!state_result)
	{
		return pal::unexpected{state_result.error()};
	}

	auto &state = **state_result;
	state.relax = opts.relax;

	if (!peer_token.empty())
	{
		state.peer_token(peer_token);
		state.cookie_secret = ctx->cookie_secret;
		::SSL_set_options(state.ssl.get(), SSL_OP_COOKIE_EXCHANGE);
	}

	return attorney::emit_handshake_channel(std::move(*state_result));
}

result<handshake_channel> make_channel (const context_ptr &ctx, const connector_handshake_options &opts) noexcept //{{{1
{
	auto state_result = make_session(ctx);
	if (!state_result)
	{
		return pal::unexpected{state_result.error()};
	}

	auto &state = **state_result;
	state.relax = opts.relax;

	if (!opts.peer_name.empty())
	{
		// SSL_set_tlsext_host_name wants a NUL-terminated string; the zero-initialized buffer plus a
		// bounded copy (size <= max_name_length, one byte to spare) leaves the trailing byte as the
		// terminator, avoiding a throwing std::string allocation.
		constexpr size_t max_name_length = 255;
		if (opts.peer_name.size() > max_name_length)
		{
			return make_unexpected(secure_channel_errc::invalid_configuration);
		}

		std::array<char, max_name_length + 1> name_buf{};
		std::memcpy(name_buf.data(), opts.peer_name.data(), opts.peer_name.size());
		const char *name_cstr = name_buf.data();

		// SNI: intended rendezvous hostname (a wire hint to the server, not a security check)
		if (::SSL_set_tlsext_host_name(state.ssl.get(), name_cstr) != 1)
		{
			::ERR_clear_error();
			return make_unexpected(secure_channel_errc::invalid_configuration);
		}

		// register expected hostname, OpenSSL will reject mismatched SAN/CN
		auto *params = ::SSL_get0_param(state.ssl.get());
		::X509_VERIFY_PARAM_set_hostflags(params, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
		if (::X509_VERIFY_PARAM_set1_host(params, name_cstr, opts.peer_name.size()) != 1)
		{
			::ERR_clear_error();
			return make_unexpected(secure_channel_errc::invalid_configuration);
		}
	}

	return attorney::emit_handshake_channel(std::move(*state_result));
}

//}}}1

} // namespace __secure_channel

result<handshake_result> handshake_channel::step_impl ( //{{{1
	std::span<const std::byte> in,
	std::span<std::byte> out) noexcept
{
	if (!impl_ || !impl_->state)
	{
		return make_unexpected(secure_channel_errc::invalid_configuration);
	}
	auto &state = *impl_->state;

	bio_io io{.read_in = in, .write_out = out};
	::BIO_set_data(state.bio, &io);

	::ERR_clear_error();
	state.verify_error = 0;
	const int ret = ::SSL_do_handshake(state.ssl.get());

	::BIO_set_data(state.bio, nullptr);

	handshake_result r{};
	r.consumed = io.read_consumed;
	r.produced = io.write_produced;
	r.want_output = io.write_overflow;

	if (ret == 1)
	{
		// handshake complete, migrate state into connected_channel.
		auto migrated = __secure_channel::attorney::emit_connected_channel(std::move(impl_->state));
		if (!migrated)
		{
			return pal::unexpected{migrated.error()};
		}
		impl_.reset();
		r.connected.emplace(std::move(*migrated));
		return r;
	}

	switch (::SSL_get_error(state.ssl.get(), ret))
	{
		case SSL_ERROR_WANT_READ:
			r.want_input = true;
			return r;

		case SSL_ERROR_WANT_WRITE:
			r.want_output = true;
			return r;

		default:
			return pal::unexpected{map_openssl_error(state)};
	}
}

result<channel_result> connected_channel::encrypt_impl ( //{{{1
	std::span<const std::byte> plain,
	std::span<std::byte> out) noexcept
{
	if (!impl_ || !impl_->state)
	{
		return make_unexpected(secure_channel_errc::invalid_configuration);
	}
	auto &state = *impl_->state;

	if (state.closed)
	{
		return make_unexpected(secure_channel_errc::closed);
	}

	channel_result r{};
	if (plain.empty())
	{
		return r;
	}

	if (state.is_datagram)
	{
		if (plain.size() > max_message_size())
		{
			return make_unexpected(secure_channel_errc::message_too_large);
		}
	}

	bio_io io{.read_in = {}, .write_out = out};
	::BIO_set_data(state.bio, &io);

	::ERR_clear_error();
	const int written = ::SSL_write(state.ssl.get(), plain.data(), static_cast<int>(plain.size()));
	const int ssl_err = ::SSL_get_error(state.ssl.get(), written);

	::BIO_set_data(state.bio, nullptr);

	r.produced = io.write_produced;
	r.peer_closed = state.peer_closed;
	if (written > 0)
	{
		r.consumed = static_cast<size_t>(written);
		if (r.consumed < plain.size())
		{
			r.want_output = true;
		}
		return r;
	}

	if (ssl_err == SSL_ERROR_WANT_WRITE || io.write_overflow)
	{
		r.want_output = true;
		::ERR_clear_error();
		return r;
	}
	else if (const auto ec = map_openssl_io_error(state, ssl_err))
	{
		return pal::unexpected{ec};
	}

	return r;
}

result<channel_result> connected_channel::decrypt_impl ( //{{{1
	std::span<const std::byte> cipher,
	std::span<std::byte> out) noexcept
{
	if (!impl_ || !impl_->state)
	{
		return make_unexpected(secure_channel_errc::invalid_configuration);
	}
	auto &state = *impl_->state;

	channel_result r{};

	auto effective_cipher = cipher;
	if (state.is_datagram && !cipher.empty())
	{
		effective_cipher = cipher.first(dtls_record_size(cipher));
	}

	bio_io io{.read_in = effective_cipher, .write_out = {}};
	::BIO_set_data(state.bio, &io);

	int total_out = 0;
	std::error_code ec;
	for (;;)
	{
		if (out.empty())
		{
			r.want_output = true;
			break;
		}

		::ERR_clear_error();
		const int got = ::SSL_read(state.ssl.get(), out.data(), static_cast<int>(out.size()));
		const int ssl_err = ::SSL_get_error(state.ssl.get(), got);

		if (got > 0)
		{
			total_out += got;
			out = out.subspan(static_cast<size_t>(got));
			if (::SSL_pending(state.ssl.get()) > 0)
			{
				continue;
			}
			break;
		}

		if (ssl_err == SSL_ERROR_WANT_READ)
		{
			// WANT_READ means the BIO returned retry, which only happens once our input is drained.
			r.want_input = true;
			break;
		}
		else if (ssl_err == SSL_ERROR_ZERO_RETURN)
		{
			state.peer_closed = true;
			break;
		}

		ec = map_openssl_io_error(state, ssl_err);
		break;
	}

	::BIO_set_data(state.bio, nullptr);
	r.consumed = io.read_consumed;
	r.produced = static_cast<size_t>(total_out);
	r.peer_closed = state.peer_closed;

	if (ec)
	{
		return pal::unexpected{ec};
	}

	return r;
}

result<channel_result> connected_channel::close_impl (std::span<std::byte> out) noexcept //{{{1
{
	if (!impl_ || !impl_->state)
	{
		return make_unexpected(secure_channel_errc::invalid_configuration);
	}
	auto &state = *impl_->state;

	channel_result r{};
	if (state.closed)
	{
		r.peer_closed = state.peer_closed;
		return r;
	}

	bio_io io{.read_in = {}, .write_out = out};
	::BIO_set_data(state.bio, &io);

	::ERR_clear_error();
	const int ret = ::SSL_shutdown(state.ssl.get());
	const int ssl_err = ::SSL_get_error(state.ssl.get(), ret);

	::BIO_set_data(state.bio, nullptr);

	r.produced = io.write_produced;
	state.closed = true;

	if (io.write_overflow)
	{
		r.want_output = true;
	}

	std::error_code ec;
	if (ret < 0 && ssl_err != SSL_ERROR_WANT_READ && ssl_err != SSL_ERROR_WANT_WRITE && !io.write_overflow)
	{
		ec = map_openssl_io_error(state, ssl_err);
	}
	r.peer_closed = state.peer_closed;

	if (ec)
	{
		return pal::unexpected{ec};
	}

	return r;
}

result<certificate> connected_channel::peer_certificate () const noexcept //{{{1
{
	if (!impl_ || !impl_->state)
	{
		return certificate{};
	}

	return __secure_channel::attorney::from_sys(::SSL_get_peer_certificate(impl_->state->ssl.get()));
}

std::string_view connected_channel::selected_protocol () const noexcept //{{{1
{
	if (!impl_ || !impl_->state)
	{
		return {};
	}

	const unsigned char *data = nullptr;
	unsigned int len = 0;
	::SSL_get0_alpn_selected(impl_->state->ssl.get(), &data, &len);

	if (data == nullptr || len == 0)
	{
		return {};
	}

	return {reinterpret_cast<const char *>(data), len};
}

size_t connected_channel::max_message_size () const noexcept //{{{1
{
	if (!impl_ || !impl_->state)
	{
		return 0;
	}

	if (impl_->state->is_datagram)
	{
		// DTLS record overhead depends on cipher; use MTU minus a generous reserve.
		constexpr size_t overhead = 64;
		return dtls_mtu > overhead ? dtls_mtu - overhead : 0;
	}

	// stream is unbounded
	return SIZE_MAX;
}

//}}}1

} // namespace pal::crypto

#endif // __pal_crypto_openssl
