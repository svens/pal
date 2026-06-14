#pragma once

/**
 * \file pal/crypto/secure_channel.hpp
 * Transport-independent TLS/DTLS secure channel
 *
 * Public API for establishing and operating (D)TLS sessions decoupled from any specific transport. The library is a
 * pure protocol parser/generator: the caller supplies input/output byte spans and drives I/O on its own.
 *
 * Lifecycle:
 *   1. Build a long-lived factory: `stream_acceptor::make(opts)` etc.
 *   2. Spawn a per-session handshake: `factory.accept(opts)` / `factory.connect(opts)`. This returns a
 *      `handshake_channel`.
 *   3. Pump `handshake_channel::step(in, out)` until the returned `handshake_result::connected` is engaged.
 *      Move it out to get a `connected_channel`.
 *   4. Use `connected_channel::encrypt/decrypt/close` for application I/O.
 *
 * Transport sniffing helpers are provided for callers that mux plain and (D)TLS traffic on the same socket.
 */

#include <pal/buffer.hpp>
#include <pal/crypto/certificate.hpp>
#include <pal/crypto/key.hpp>
#include <pal/result.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <system_error>
#include <utility>

namespace pal::crypto
{

// transport {{{1

/// Selects stream (TLS) or datagram (DTLS) protocol for factory and session templates.
enum class transport
{
	stream,
	datagram,
};

// verify_relax {{{1

/// Bitmask controlling peer certificate verification relaxations.
///
/// Default `none` requires strict trust-chain verification. Bits enable
/// specific deviations; combine with bitwise operators.
enum class verify_relax : unsigned
{
	none = 0,

	/// Accept a self-signed leaf certificate as if it were trusted.
	/// Maps to OpenSSL `X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT`.
	self_signed = 1U << 0,
};

constexpr verify_relax operator| (verify_relax a, verify_relax b) noexcept
{
	return static_cast<verify_relax>(std::to_underlying(a) | std::to_underlying(b));
}

constexpr verify_relax operator& (verify_relax a, verify_relax b) noexcept
{
	return static_cast<verify_relax>(std::to_underlying(a) & std::to_underlying(b));
}

constexpr verify_relax operator~(verify_relax a) noexcept
{
	return static_cast<verify_relax>(~std::to_underlying(a));
}

constexpr bool any (verify_relax a) noexcept
{
	return std::to_underlying(a) != 0;
}

// error category {{{1

/// Secure channel error codes.
#define __pal_secure_channel_errc(Impl) \
	Impl(__0, "internal placeholder for not an error") \
	Impl(handshake_failed, "TLS handshake failed") \
	Impl(peer_verification_failed, "peer certificate verification failed") \
	Impl(peer_hostname_mismatch, "peer hostname does not match certificate") \
	Impl(no_application_protocol, "no overlapping ALPN protocol") \
	Impl(client_certificate_required, "client certificate required but not provided") \
	Impl(message_too_large, "message exceeds datagram MTU") \
	Impl(decrypt_failed, "record decryption failed") \
	Impl(protocol_error, "secure channel protocol error") \
	Impl(closed, "secure channel is closed") \
	Impl(invalid_configuration, "invalid secure channel configuration")

enum class secure_channel_errc : int
{
#define __pal_secure_channel_errc_enum(Code, Message) Code,
	__pal_secure_channel_errc(__pal_secure_channel_errc_enum)
#undef __pal_secure_channel_errc_enum
};

/// Return secure channel error category.
const std::error_category &secure_channel_category () noexcept;

/// Make `std::error_code` from `secure_channel_errc`.
inline std::error_code make_error_code (secure_channel_errc ec) noexcept
{
	return {static_cast<int>(ec), secure_channel_category()};
}

// wire protocol sniffing {{{1

/// Result of byte sniffing for protocol classification.
enum class wire_protocol
{
	/// Insufficient bytes to decide; feed more (stream sniff only).
	need_more,

	/// Bytes do not look like (D)TLS.
	plain,

	/// Bytes look like a TLS record.
	tls,

	/// Bytes look like a DTLS record.
	dtls,
};

/// Classify the leading bytes of a stream-style connection.
///
/// Returns `need_more` if fewer than 6 bytes are available, `tls` if the bytes match a TLS ClientHello record
/// header, otherwise `plain`. The function does not consume input; the caller retains its read buffer.
constexpr wire_protocol sniff_stream (const_buffer auto const &input) noexcept
{
	constexpr uint8_t handshake = 22;
	constexpr uint8_t version_high = 0x03;
	constexpr uint8_t version_low_max = 0x04;
	constexpr size_t min_bytes = 6;
	constexpr uint8_t client_hello = 1;
	constexpr size_t handshake_type_offset = 5;

	const auto n = std::size(input);
	const auto *p = reinterpret_cast<const uint8_t *>(std::data(input));

	if (n < 1)
	{
		return wire_protocol::need_more;
	}
	if (p[0] != handshake)
	{
		return wire_protocol::plain;
	}
	if (n < 3)
	{
		return wire_protocol::need_more;
	}
	if (p[1] != version_high || p[2] > version_low_max)
	{
		return wire_protocol::plain;
	}
	if (n < min_bytes)
	{
		return wire_protocol::need_more;
	}
	if (p[handshake_type_offset] != client_hello)
	{
		return wire_protocol::plain;
	}
	return wire_protocol::tls;
}

/// Classify a single datagram payload.
///
/// Verdict is immediate (never returns `need_more`): returns `dtls` only if the payload matches a DTLS
/// ClientHello record header, otherwise `plain`.
constexpr wire_protocol sniff_datagram (const_buffer auto const &input) noexcept
{
	constexpr uint8_t handshake = 22;
	constexpr uint8_t version_high = 0xFE;
	constexpr uint8_t version_low_min = 0xFC;
	constexpr size_t header_size = 13;
	constexpr uint8_t client_hello = 1;
	constexpr size_t length_offset = 11;

	const auto n = std::size(input);
	const auto *p = reinterpret_cast<const uint8_t *>(std::data(input));

	if (n < header_size)
	{
		return wire_protocol::plain;
	}
	if (p[0] != handshake)
	{
		return wire_protocol::plain;
	}
	if (p[1] != version_high || p[2] < version_low_min)
	{
		return wire_protocol::plain;
	}
	if (p[3] != 0 || p[4] != 0)
	{
		// Epoch 0 expected for first record from client.
		return wire_protocol::plain;
	}
	const size_t length = (static_cast<size_t>(p[length_offset]) << 8U) | static_cast<size_t>(p[length_offset + 1]);
	if (length < 1 || length + header_size > n)
	{
		return wire_protocol::plain;
	}
	if (p[header_size] != client_hello)
	{
		return wire_protocol::plain;
	}
	return wire_protocol::dtls;
}

// options {{{1

/// Long-lived context options for `acceptor`.
struct acceptor_options
{
	/// Server certificate chain (leaf first).
	std::span<const certificate> certificate_chain;

	/// Private key matching the leaf certificate.
	key private_key;

	/// Demand a client certificate (mTLS).
	bool require_client_certificate = false;

	/// Trusted roots for verifying client certificates (mTLS).
	std::span<const certificate> trusted_roots = {};

	/// Augment client-cert verification with system trust store (mTLS).
	bool use_system_trust = false;

	/// ALPN protocols this server will accept, in preference order.
	std::span<const std::string_view> supported_protocols = {};
};

/// Per-handshake options for `acceptor`.
struct acceptor_handshake_options
{
	/// Verification relaxations applied to the client certificate (mTLS).
	verify_relax relax = verify_relax::none;
};

/// Long-lived context options for `connector`.
struct connector_options
{
	/// Trusted roots for verifying the server certificate.
	std::span<const certificate> trusted_roots = {};

	/// Augment server-cert verification with system trust store.
	bool use_system_trust = true;

	/// Optional client certificate chain to present (mTLS).
	std::span<const certificate> certificate_chain = {};

	/// Private key matching the leaf client certificate (mTLS).
	key private_key = {};

	/// ALPN protocols to advertise, in preference order.
	std::span<const std::string_view> supported_protocols = {};
};

/// Per-handshake options for `connector`.
struct connector_handshake_options
{
	/// Server name for SNI and hostname verification.
	std::string_view peer_name = {};

	/// Verification relaxations applied to the server certificate.
	verify_relax relax = verify_relax::none;
};

// forward declarations {{{1

class handshake_channel;
class connected_channel;

namespace __secure_channel
{

/// Attorney-Client idiom bridge
struct attorney;

/// Internal opaque context shared between handshake_channel/connected_channel and their producing factory.
/// Holds the backend SSL_CTX (or equivalent) and owned cert/key material.
struct context;
using context_ptr = std::shared_ptr<context>;

result<context_ptr> make_context (transport t, const acceptor_options &opts) noexcept;
result<context_ptr> make_context (transport t, const connector_options &opts) noexcept;

result<handshake_channel> make_channel (const context_ptr &ctx, const acceptor_handshake_options &opts) noexcept;
result<handshake_channel> make_channel (const context_ptr &ctx, const connector_handshake_options &opts) noexcept;

/// \internal
/// \note: assumes the DTLS 1.0/1.2 record header: fixed size, fixed offset
///
/// \todo DTLS 1.3 (RFC 9147) protected records use a variable-length unified header and are NOT handled here.
/// This is safe while the supported backends cannot negotiate DTLS 1.3; revisit when a 1.3-capable backend lands.
/// The crypto/secure_channel/datagram/decrypt_multi_record test is the end-to-end trip-wire.
constexpr size_t dtls_record_size (std::span<const std::byte> bytes) noexcept
{
	constexpr size_t header_size = 13;
	constexpr size_t length_offset = 11;

	if (bytes.size() < header_size)
	{
		return bytes.size();
	}

	// 16-bit big-endian record length
	const auto length = (std::to_integer<size_t>(bytes[length_offset]) << 8U)
		| (std::to_integer<size_t>(bytes[length_offset + 1]));

	const auto claimed = length + header_size;
	return claimed > bytes.size() ? bytes.size() : claimed;
}

} // namespace __secure_channel

// channel_result {{{1

/// Outcome of `connected_channel::encrypt/decrypt/close`.
struct channel_result
{
	/// Bytes consumed from input.
	size_t consumed = 0;

	/// Bytes produced into output.
	size_t produced = 0;

	/// Operation needs more peer bytes (decrypt with partial record).
	bool want_input = false;

	/// Output buffer was insufficient; supply a fresh output and call again.
	bool want_output = false;

	/// `decrypt()` observed a peer `close_notify`. Further reads from peer are not expected.
	bool peer_closed = false;
};

// connected_channel {{{1

/// Established (D)TLS session, ready for application I/O.
///
/// Move-only. Produced by `handshake_channel::step()` when the handshake completes. The destructor does not emit
/// `close_notify`; callers wanting a clean shutdown must invoke `close()` and emit its output bytes first.
class connected_channel
{
public:

	connected_channel ();
	connected_channel (connected_channel &&) noexcept;
	connected_channel &operator= (connected_channel &&) noexcept;
	~connected_channel () noexcept;

	connected_channel (const connected_channel &) = delete;
	connected_channel &operator= (const connected_channel &) = delete;

	/// Returns true if this represents an uninitialised channel.
	[[nodiscard]] bool is_null () const noexcept
	{
		return impl_ == nullptr;
	}

	/// Returns true if this is a valid (non-null) channel.
	explicit operator bool () const noexcept
	{
		return !is_null();
	}

	/// Encrypt application bytes from `plain` into `out`.
	///
	/// For datagram channels, `plain.size()` must not exceed `max_message_size()` or the call returns
	/// `secure_channel_errc::message_too_large`. Empty `plain` is a legal no-op. Empty `out` yields `want_output =
	/// true`.
	///
	/// Returns `secure_channel_errc::closed` if `close()` has been called.
	result<channel_result> encrypt (const_buffer auto const &plain, mutable_buffer auto &&out) noexcept
	{
		return encrypt_impl(std::as_bytes(std::span{plain}), std::as_writable_bytes(std::span{out}));
	}

	/// Decrypt peer ciphertext from `cipher` into `out`.
	///
	/// For datagram channels, only a single record's payload is consumed per call; the caller iterates for
	/// multi-record datagrams. Empty `out` yields `want_output = true`.
	///
	/// Returns `channel_result::peer_closed = true` when a `close_notify` has been observed.
	result<channel_result> decrypt (const_buffer auto const &cipher, mutable_buffer auto &&out) noexcept
	{
		return decrypt_impl(std::as_bytes(std::span{cipher}), std::as_writable_bytes(std::span{out}));
	}

	/// Drain pending decrypted bytes into `out` without consuming new peer ciphertext. Useful when a previous
	/// `decrypt()` call set `want_output = true` because the destination buffer was too small to hold an entire
	/// record's decrypted payload.
	result<channel_result> decrypt (mutable_buffer auto &&out) noexcept
	{
		return decrypt_impl({}, std::as_writable_bytes(std::span{out}));
	}

	/// Generate a `close_notify` alert into `out`.
	///
	/// Subsequent `encrypt()` calls return `secure_channel_errc::closed`. `decrypt()` continues to function so
	/// the caller may observe the peer's `close_notify` and any in-flight data.
	result<channel_result> close (mutable_buffer auto &&out) noexcept
	{
		return close_impl(std::as_writable_bytes(std::span{out}));
	}

	/// Return the verified peer certificate, or a null certificate if no peer cert was presented
	/// (e.g. acceptor without mTLS).
	[[nodiscard]] result<certificate> peer_certificate () const noexcept;

	/// Return the ALPN-selected protocol, or an empty view if none was negotiated.
	[[nodiscard]] std::string_view selected_protocol () const noexcept;

	/// Return the maximum plaintext size accepted by `encrypt()`. For stream channels this is `SIZE_MAX`. For
	/// datagram channels it is the configured MTU minus DTLS framing overhead.
	[[nodiscard]] size_t max_message_size () const noexcept;

private:

	struct impl_type;
	using impl_ptr = std::unique_ptr<impl_type>;
	impl_ptr impl_;

	explicit connected_channel (impl_ptr impl) noexcept;

	result<channel_result> encrypt_impl (std::span<const std::byte> plain, std::span<std::byte> out) noexcept;
	result<channel_result> decrypt_impl (std::span<const std::byte> cipher, std::span<std::byte> out) noexcept;
	result<channel_result> close_impl (std::span<std::byte> out) noexcept;

	friend struct __secure_channel::attorney;
	friend class handshake_channel;
};

// handshake_result {{{1

/// Outcome of `handshake_channel::step()`.
///
/// The `connected` field is engaged on (and only on) the step that finishes the handshake. Once engaged, the source
/// `handshake_channel` is exhausted and must not be used further.
struct handshake_result
{
	/// Bytes consumed from the caller's input span.
	size_t consumed = 0;

	/// Bytes written to the caller's output span.
	size_t produced = 0;

	/// Channel needs more peer bytes to make progress.
	bool want_input = false;

	/// Output span was insufficient; call again with a fresh output buffer.
	bool want_output = false;

	/// Engaged on the step that completes the handshake.
	std::optional<connected_channel> connected;
};

// handshake_channel {{{1

/// Driver for the (D)TLS handshake phase.
///
/// Move-only. Consumed when the handshake completes: the resulting `connected_channel` is delivered via
/// `handshake_result::connected` and the `handshake_channel` instance must not be reused afterwards.
class handshake_channel
{
public:

	handshake_channel ();
	handshake_channel (handshake_channel &&) noexcept;
	handshake_channel &operator= (handshake_channel &&) noexcept;
	~handshake_channel () noexcept;

	handshake_channel (const handshake_channel &) = delete;
	handshake_channel &operator= (const handshake_channel &) = delete;

	/// Returns true if this represents an uninitialised channel.
	[[nodiscard]] bool is_null () const noexcept
	{
		return impl_ == nullptr;
	}

	/// Returns true if this is a valid (non-null) channel.
	explicit operator bool () const noexcept
	{
		return !is_null();
	}

	/// Drive one step of the handshake.
	///
	/// Consumes peer bytes from `in` and writes negotiation output to `out`. Either buffer may be empty: empty
	/// `in` is legal (e.g. client first call), and empty `out` simply yields `want_output = true`.
	///
	/// On the step that completes the handshake, the returned `handshake_result::connected` is engaged with the
	/// resulting `connected_channel`. The current `handshake_channel` is then exhausted.
	result<handshake_result> step (const_buffer auto const &in, mutable_buffer auto &&out) noexcept
	{
		return step_impl(std::as_bytes(std::span{in}), std::as_writable_bytes(std::span{out}));
	}

	/// Drive one step of the handshake with no peer input (client first call, or to drain pending output into a
	/// fresh buffer).
	result<handshake_result> step (mutable_buffer auto &&out) noexcept
	{
		return step_impl({}, std::as_writable_bytes(std::span{out}));
	}

private:

	struct impl_type;
	using impl_ptr = std::unique_ptr<impl_type>;
	impl_ptr impl_;

	explicit handshake_channel (impl_ptr impl) noexcept;

	result<handshake_result> step_impl (std::span<const std::byte> in, std::span<std::byte> out) noexcept;

	friend result<handshake_channel> __secure_channel::make_channel (
		const __secure_channel::context_ptr &, const acceptor_handshake_options &
	) noexcept;
	friend result<handshake_channel> __secure_channel::make_channel (
		const __secure_channel::context_ptr &, const connector_handshake_options &
	) noexcept;
	friend struct __secure_channel::attorney;
	friend class connected_channel;
};

// acceptor {{{1

/// Long-lived factory for server-side (D)TLS sessions.
///
/// Holds the backend security context (certificate chain, trust material, ALPN list, etc.). Construct once and reuse
/// for many sessions via `accept()`. Obtain via `make()`; cheap to copy/move (`std::shared_ptr` semantics internally).
template <transport T>
class acceptor
{
public:

	static constexpr transport transport_value = T;

	using options = acceptor_options;
	using handshake_options = acceptor_handshake_options;

	acceptor (const acceptor &) = default;
	acceptor &operator= (const acceptor &) = default;
	acceptor (acceptor &&) noexcept = default;
	acceptor &operator= (acceptor &&) noexcept = default;

	/// Construct a new acceptor with the given context options.
	static result<acceptor> make (const options &opts) noexcept
	{
		return __secure_channel::make_context(T, opts).transform(&to_api);
	}

	/// Create a new server-side `handshake_channel`. No I/O is performed.
	[[nodiscard]] result<handshake_channel> accept (const handshake_options &opts = {}) const noexcept
	{
		return __secure_channel::make_channel(ctx_, opts);
	}

private:

	__secure_channel::context_ptr ctx_;

	explicit acceptor (__secure_channel::context_ptr ctx) noexcept
		: ctx_{std::move(ctx)}
	{
	}

	static acceptor to_api (__secure_channel::context_ptr ctx) noexcept
	{
		return acceptor{std::move(ctx)};
	}
};

// connector {{{1

/// Long-lived factory for client-side (D)TLS sessions.
template <transport T>
class connector
{
public:

	static constexpr transport transport_value = T;

	using options = connector_options;
	using handshake_options = connector_handshake_options;

	connector (const connector &) = default;
	connector &operator= (const connector &) = default;
	connector (connector &&) noexcept = default;
	connector &operator= (connector &&) noexcept = default;

	/// Construct a new connector with the given context options.
	static result<connector> make (const options &opts) noexcept
	{
		return __secure_channel::make_context(T, opts).transform(&to_api);
	}

	/// Create a new client-side `handshake_channel`. No I/O is performed.
	[[nodiscard]] result<handshake_channel> connect (const handshake_options &opts = {}) const noexcept
	{
		return __secure_channel::make_channel(ctx_, opts);
	}

private:

	__secure_channel::context_ptr ctx_;

	explicit connector (__secure_channel::context_ptr ctx) noexcept
		: ctx_{std::move(ctx)}
	{
	}

	static connector to_api (__secure_channel::context_ptr ctx) noexcept
	{
		return connector{std::move(ctx)};
	}
};

// public aliases {{{1

using stream_acceptor = acceptor<transport::stream>;
using stream_connector = connector<transport::stream>;
using datagram_acceptor = acceptor<transport::datagram>;
using datagram_connector = connector<transport::datagram>;

// }}}1

} // namespace pal::crypto

namespace std
{

template <>
struct is_error_code_enum<pal::crypto::secure_channel_errc>: true_type
{
};

} // namespace std
