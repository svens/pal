#pragma once

/**
 * \file pal/net/ip/basic_endpoint.hpp
 * IP endpoint (address/port pair)
 */

#include <pal/net/__socket.hpp>
#include <pal/net/concepts.hpp>
#include <pal/net/ip/address.hpp>
#include <pal/net/ip/port_type.hpp>
#include <pal/byte_order.hpp>
#include <pal/hash.hpp>
#include <pal/result.hpp>
#include <algorithm>
#include <compare>
#include <cstdint>
#include <cstring>
#include <format>
#include <span>

#if __pal_net_posix
	#include <netinet/in.h>
#elif __pal_net_winsock
	#include <ws2ipdef.h>
#endif

namespace pal::net::ip
{

namespace __endpoint
{

using v4 = ::sockaddr_in;
using v6 = ::sockaddr_in6;

constexpr ::in_addr to_native (const address_v4 &a) noexcept
{
#if __pal_net_posix
	return {.s_addr = hton(a.to_uint())};
#else
	const auto &b = a.to_bytes();
	return {.S_un = {.S_un_b = {b[0], b[1], b[2], b[3]}}};
#endif
}

constexpr ::in6_addr to_native (const address_v6 &a) noexcept
{
	::in6_addr r{};
	std::ranges::copy(a.to_bytes(), r.s6_addr);
	return r;
}

constexpr address_v4 from_native (const v4 &sa) noexcept
{
#if __pal_net_posix
	return address_v4{ntoh(sa.sin_addr.s_addr)};
#else
	const auto &b = sa.sin_addr.S_un.S_un_b;
	return address_v4{address_v4::bytes_type{b.s_b1, b.s_b2, b.s_b3, b.s_b4}};
#endif
}

constexpr address_v6 from_native (const v6 &sa) noexcept
{
	address_v6::bytes_type b{};
	std::ranges::copy(sa.sin6_addr.s6_addr, b.begin());
	return address_v6{b, sa.sin6_scope_id};
}

constexpr std::strong_ordering operator<=> (const v4 &a, const v4 &b) noexcept
{
	if (auto r = from_native(a) <=> from_native(b); r != 0)
	{
		return r;
	}
	return ntoh(a.sin_port) <=> ntoh(b.sin_port);
}

constexpr std::strong_ordering operator<=> (const v6 &a, const v6 &b) noexcept
{
	if (auto r = from_native(a) <=> from_native(b); r != 0)
	{
		return r;
	}
	return ntoh(a.sin6_port) <=> ntoh(b.sin6_port);
}

constexpr uint64_t hash (const v4 &a) noexcept
{
	return hash_128_to_64(
		static_cast<uint64_t>(AF_INET),
		hash_128_to_64(static_cast<uint64_t>(ntoh(a.sin_port)), from_native(a).hash())
	);
}

constexpr uint64_t hash (const v6 &a) noexcept
{
	return hash_128_to_64(
		static_cast<uint64_t>(AF_INET6),
		hash_128_to_64(
			static_cast<uint64_t>(ntoh(a.sin6_port)),
			hash_128_to_64(fnv_1a_64(a.sin6_addr.s6_addr), static_cast<uint64_t>(a.sin6_scope_id))
		)
	);
}

[[nodiscard]] char *ntop (const v4 &a, char *first, char *last) noexcept;
[[nodiscard]] char *ntop (const v6 &a, char *first, char *last) noexcept;

} // namespace __endpoint

/// Protocol-specific IP address/port pair. \a Protocol must satisfy
/// pal::net::protocol
///
/// \internal The union stores either sockaddr_in or sockaddr_in6 directly,
/// exploiting the common initial sequence of sin_family/sin6_family. The
/// IPv6 constexpr path is runtime-only: methods that read family() via
/// v4_.sin_family have undefined behaviour in constant evaluation when v6_
/// is the active union member.
template <typename Protocol>
class basic_endpoint
{
public:

	/// Endpoint's internet protocol
	using protocol_type = Protocol;

	/// Maximum human readable textual representation length
	static constexpr size_t max_string_length = address_v6::max_string_length + sizeof("[]:65535") - 1;

	/// Construct endpoint with unspecified IPv4 address and port 0
	constexpr basic_endpoint () noexcept
		: v4_{}
	{
		v4_.sin_family = AF_INET;
	}

	/// Construct endpoint with IPv4 \a address and \a port
	constexpr basic_endpoint (const address_v4 &address, port_type port) noexcept
		: v4_{}
	{
		v4_.sin_family = AF_INET;
		v4_.sin_addr = __endpoint::to_native(address);
		v4_.sin_port = hton(static_cast<uint16_t>(port));
	}

	/// Construct endpoint with IPv6 \a address and \a port
	constexpr basic_endpoint (const address_v6 &address, port_type port) noexcept
		: v6_{}
	{
		v6_.sin6_family = AF_INET6;
		v6_.sin6_addr = __endpoint::to_native(address);
		v6_.sin6_port = hton(static_cast<uint16_t>(port));
		v6_.sin6_scope_id = address.scope_id();
	}

	/// Construct endpoint with IP \a address and \a port
	constexpr basic_endpoint (const address &address, port_type port) noexcept
	{
		if (const auto *v4 = address.v4())
		{
			v4_ = {};
			v4_.sin_family = AF_INET;
			v4_.sin_addr = __endpoint::to_native(*v4);
			v4_.sin_port = hton(static_cast<uint16_t>(port));
		}
		else
		{
			v6_ = {};
			v6_.sin6_family = AF_INET6;
			const auto *v6 = address.v6();
			v6_.sin6_addr = __endpoint::to_native(*v6);
			v6_.sin6_port = hton(static_cast<uint16_t>(port));
			v6_.sin6_scope_id = v6->scope_id();
		}
	}

	/// Construct endpoint with \a protocol family's unspecified address and \a port
	constexpr basic_endpoint (const protocol_type &protocol, port_type port) noexcept
	{
		if (protocol.family() == AF_INET)
		{
			v4_ = {};
			v4_.sin_family = AF_INET;
			v4_.sin_port = hton(static_cast<uint16_t>(port));
		}
		else
		{
			v6_ = {};
			v6_.sin6_family = AF_INET6;
			v6_.sin6_port = hton(static_cast<uint16_t>(port));
		}
	}

	/// Return endpoint protocol instance
	[[nodiscard]] constexpr protocol_type protocol () const noexcept
	{
		return protocol_type{family()};
	}

	/// Return endpoint address
	[[nodiscard]] constexpr ip::address address () const noexcept
	{
		if (is_v4())
		{
			return __endpoint::from_native(v4_);
		}
		return __endpoint::from_native(v6_);
	}

	/// Set new endpoint address, keeping port unchanged
	constexpr void address (const address_v4 &address) noexcept
	{
		v4_.sin_family = AF_INET;
		v4_.sin_addr = __endpoint::to_native(address);
	}

	/// Set new endpoint address, keeping port unchanged
	constexpr void address (const address_v6 &address) noexcept
	{
		v6_.sin6_family = AF_INET6;
		v6_.sin6_addr = __endpoint::to_native(address);
		v6_.sin6_scope_id = address.scope_id();
	}

	/// Set new endpoint address, keeping port unchanged
	constexpr void address (const ip::address &address) noexcept
	{
		if (const auto *v4 = address.v4())
		{
			this->address(*v4);
		}
		else
		{
			this->address(*address.v6());
		}
	}

	/// Return endpoint port
	[[nodiscard]] constexpr port_type port () const noexcept
	{
		return static_cast<port_type>(ntoh(v4_.sin_port));
	}

	/// Set new endpoint port, keeping address unchanged
	constexpr void port (port_type port) noexcept
	{
		v4_.sin_port = hton(static_cast<uint16_t>(port));
	}

	/// Return pointer to socket address storage
	[[nodiscard]] void *data () noexcept
	{
		return this;
	}

	/// Return pointer to socket address storage
	[[nodiscard]] const void *data () const noexcept
	{
		return this;
	}

	/// Return size of active socket address structure
	[[nodiscard]] constexpr size_t size () const noexcept
	{
		return is_v4() ? sizeof(v4_) : sizeof(v6_);
	}

	/// Confirm socket address data area size after OS write.
	[[nodiscard]] result<void> resize (size_t new_size) noexcept
	{
		if (new_size == size())
		{
			return {};
		}
		return make_unexpected(std::errc::invalid_argument);
	}

	/// Return socket address storage capacity
	[[nodiscard]] constexpr size_t capacity () const noexcept
	{
		return sizeof(*this);
	}

	/// Convert endpoint to human readable textual representation
	[[nodiscard]] std::to_chars_result to_chars (char *first, char *last) const noexcept
	{
		if (auto end = is_v4() ? __endpoint::ntop(v4_, first, last) : __endpoint::ntop(v6_, first, last))
		{
			return {.ptr = end, .ec = std::errc{}};
		}
		return {.ptr = last, .ec = std::errc::value_too_large};
	}

	/// Compare \a this to \a that
	[[nodiscard]] constexpr std::strong_ordering operator<=> (const basic_endpoint &that) const noexcept
	{
		if (auto r = family() <=> that.family(); r != 0)
		{
			return r;
		}
		return is_v4() ? __endpoint::operator<=> (v4_, that.v4_) : __endpoint::operator<=> (v6_, that.v6_);
	}

	/// Return true if \a this equals \a that
	[[nodiscard]] constexpr bool operator== (const basic_endpoint &that) const noexcept
	{
		return (*this <=> that) == std::strong_ordering::equal;
	}

	/// Return hash value
	[[nodiscard]] constexpr uint64_t hash () const noexcept
	{
		return is_v4() ? __endpoint::hash(v4_) : __endpoint::hash(v6_);
	}

	/// Return copy of \a this with GDPR-safe masked address
	[[nodiscard]] constexpr basic_endpoint masked () const noexcept
	{
		return basic_endpoint{address().masked(), port()};
	}

private:

	union
	{
		__endpoint::v4 v4_;
		__endpoint::v6 v6_;
	};

	[[nodiscard]] constexpr int family () const noexcept
	{
		return v4_.sin_family;
	}

	[[nodiscard]] constexpr bool is_v4 () const noexcept
	{
		return family() == AF_INET;
	}
};

/// Serialize \a ep into \a out as a fixed, injective, deterministic identity token for binding the DTLS
/// anti-amplification cookie (a `pal::crypto::peer_token_source` adapter, found by ADL). Identity-only by
/// construction (family, address, port and IPv6 scope) so it never carries sockaddr padding or the IPv6
/// flow label and is reproducible across handshake retries. Distinct peers map to distinct tokens (the
/// family byte separates v4/v6 and their zero-padding). Returns the byte count written, or 0 if \a out is
/// too small.
template <typename Protocol>
[[nodiscard]] size_t to_peer_token (const basic_endpoint<Protocol> &ep, std::span<std::byte> out) noexcept
{
	// fixed layout, injective across v4/v6 (the family byte separates them and their zero-padding):
	constexpr size_t family_offset = 0;
	constexpr size_t address_offset = 1;
	constexpr size_t port_offset = 17;
	constexpr size_t scope_offset = 19;
	constexpr size_t token_size = 23;
	constexpr std::byte v4_family{4};
	constexpr std::byte v6_family{6};

	if (out.size() < token_size)
	{
		return 0;
	}
	std::ranges::fill(out.first(token_size), std::byte{});

	if (const auto addr = ep.address(); const auto *v4 = addr.v4())
	{
		out[family_offset] = v4_family;
		const auto &bytes = v4->to_bytes();
		std::memcpy(out.data() + address_offset, bytes.data(), bytes.size());
	}
	else
	{
		const auto *v6 = addr.v6();
		out[family_offset] = v6_family;
		const auto &bytes = v6->to_bytes();
		std::memcpy(out.data() + address_offset, bytes.data(), bytes.size());
		const auto scope = static_cast<uint32_t>(v6->scope_id());
		std::memcpy(out.data() + scope_offset, &scope, sizeof(scope));
	}

	const auto port = static_cast<uint16_t>(ep.port());
	std::memcpy(out.data() + port_offset, &port, sizeof(port));

	return token_size;
}

} // namespace pal::net::ip

namespace std
{

template <typename Protocol>
struct hash<pal::net::ip::basic_endpoint<Protocol>>
{
	[[nodiscard]] size_t operator() (const pal::net::ip::basic_endpoint<Protocol> &e) const noexcept
	{
		return e.hash();
	}
};

template <typename Protocol>
struct formatter<pal::net::ip::basic_endpoint<Protocol>>
{
	template <typename ParseContext>
	static constexpr ParseContext::iterator parse (ParseContext &ctx)
	{
		auto it = ctx.begin();
		if (it != ctx.end() && *it != '}')
		{
			throw std::format_error("invalid format for pal::net::ip::basic_endpoint");
		}
		return it;
	}

	template <typename FormatContext>
	FormatContext::iterator format (const pal::net::ip::basic_endpoint<Protocol> &e, FormatContext &ctx) const
	{
		std::array<char, pal::net::ip::basic_endpoint<Protocol>::max_string_length + 1> text{};
		auto [end, _] = e.to_chars(text.data(), text.data() + text.size());
		return std::copy(text.data(), end, ctx.out());
	}
};

} // namespace std
