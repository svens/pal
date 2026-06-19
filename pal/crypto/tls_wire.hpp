#pragma once

/**
 * \file pal/crypto/tls_wire.hpp
 * Lightweight TLS/DTLS wire-level vocabulary: transport selector, verification
 * policy, and protocol-sniffing helpers. No session or certificate machinery.
 */

#include <pal/buffer.hpp>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

namespace pal::crypto
{

// transport {{{1

/// Selects stream (TLS) or datagram (DTLS) protocol for factory and session templates.
enum class transport : int
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

// wire_protocol {{{1

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

// dtls_record_size {{{1

/// Return the byte length of the first DTLS record in \a bytes.
///
/// If \a bytes is shorter than the 13-byte DTLS record header, returns \c bytes.size() (partial header).
/// If the claimed payload length would exceed \a bytes, also returns \c bytes.size() (truncated record).
///
/// \note Assumes DTLS 1.0/1.2 fixed-size record headers.
/// \todo DTLS 1.3 (RFC 9147) uses a variable-length unified header and is not handled here.
///       Revisit when a 1.3-capable backend lands; the decrypt_multi_record test is the trip-wire.
constexpr size_t dtls_record_size (const_buffer auto const &input) noexcept
{
	constexpr size_t header_size = 13;
	constexpr size_t length_offset = 11;

	const auto bytes = std::as_bytes(std::span{input});

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

// }}}1

} // namespace pal::crypto
