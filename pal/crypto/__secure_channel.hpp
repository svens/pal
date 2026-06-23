#pragma once

/**
 * \file pal/crypto/__secure_channel.hpp
 * Backend-shared internal vocabulary for the (D)TLS secure channel (internal).
 *
 * \internal Included by the platform backends (secure_channel.openssl.cpp / secure_channel.windows.cpp),
 * never by the public header — keeps these implementation details off the public API surface.
 */

#include <pal/crypto/tls_wire.hpp>
#include <cstddef>
#include <cstring>
#include <optional>
#include <span>
#include <string_view>

namespace pal::crypto::__secure_channel
{

// (D)TLS channel kind: the {stream, datagram} x {acceptor, connector} matrix both backends switch on.
enum class kind
{
	stream_acceptor,
	stream_connector,
	datagram_acceptor,
	datagram_connector,
};

constexpr bool is_acceptor (kind k) noexcept
{
	return k == kind::stream_acceptor || k == kind::datagram_acceptor;
}

constexpr bool is_datagram (kind k) noexcept
{
	return k == kind::datagram_acceptor || k == kind::datagram_connector;
}

// Compose the matrix cell from its two axes (inverse of is_acceptor()/is_datagram()).
constexpr kind make_kind (transport t, bool acceptor) noexcept
{
	if (t == transport::stream)
	{
		return acceptor ? kind::stream_acceptor : kind::stream_connector;
	}
	return acceptor ? kind::datagram_acceptor : kind::datagram_connector;
}

// Encode ALPN \a protocols into the length-prefixed wire form `[len][name]...` at the start of \a out.
// Returns the number of bytes written (0 for an empty list), or nullopt on an empty/oversized name (a
// single name must fit one length byte) or when the encoding would overflow \a out.
inline std::optional<size_t>
encode_alpn_wire (std::span<const std::string_view> protocols, std::span<std::byte> out) noexcept
{
	constexpr size_t max_name_size = 255;

	size_t pos = 0;
	for (const auto &p: protocols)
	{
		if (p.empty() || p.size() > max_name_size || pos + p.size() + 1 > out.size())
		{
			return std::nullopt;
		}

		out[pos++] = static_cast<std::byte>(p.size());
		std::memcpy(out.data() + pos, p.data(), p.size());
		pos += p.size();
	}

	return pos;
}

} // namespace pal::crypto::__secure_channel
