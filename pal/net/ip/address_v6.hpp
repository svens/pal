#pragma once

/**
 * \file pal/net/ip/address_v6.hpp
 * IPv6 address
 */

#include <pal/net/ip/address_v4.hpp>
#include <pal/hash.hpp>
#include <pal/masked_formatter.hpp>
#include <pal/result.hpp>
#include <algorithm>
#include <array>
#include <charconv>
#include <compare>
#include <format>
#include <string_view>

namespace pal::net::ip
{

namespace __address_v6
{

[[nodiscard]] char *ntop (const uint8_t *bytes, char *first, char *last) noexcept;

} // namespace __address_v6

// NOLINTBEGIN(readability-magic-numbers)

/// IPv6 address
class address_v6
{
public:

	/// Integer type for zone/scope identifiers
	using scope_id_type = uint_least32_t;

	/// Maximum human readable representation length (address + optional %scope_id)
	static constexpr size_t max_string_length = sizeof("ffff:ffff:ffff:ffff:ffff:ffff:255.255.255.255") - 1
		+ 1   // '%'
		+ 10; // digits in UINT32_MAX

	/// Binary representation of IPv6 address
	struct bytes_type: std::array<uint8_t, 16>
	{
		bytes_type () noexcept = default;

		/// Construct address from exactly sixteen byte values
		template <typename... T>
			requires(sizeof...(T) == 16)
		explicit constexpr bytes_type(T... v) noexcept
			: std::array<uint8_t, 16>{{static_cast<uint8_t>(v)...}}
		{
		}
	};

	/// Construct new unspecified address
	constexpr address_v6 () noexcept
		: bytes_{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
		, scope_id_{}
	{
	}

	/// Construct new address from \a bytes with optional \a scope_id
	constexpr address_v6 (const bytes_type &bytes, scope_id_type scope_id = 0) noexcept
		: bytes_{bytes}
		, scope_id_{scope_id}
	{
	}

	constexpr address_v6 (const address_v6 &) noexcept = default;
	address_v6 &operator= (const address_v6 &) noexcept = default;

	/// Return binary representation of \a this address
	[[nodiscard]] constexpr const bytes_type &to_bytes () const noexcept
	{
		return bytes_;
	}

	/// Return zone/scope identifier of \a this address
	[[nodiscard]] constexpr scope_id_type scope_id () const noexcept
	{
		return scope_id_;
	}

	/**
	 * Convert \a this address to human readable textual representation by
	 * filling range [\a first, \a last). Appends %N if scope_id is non-zero.
	 *
	 * On success, returns std::to_chars_result with ptr pointing past
	 * last filled character and ec default value initialized.
	 *
	 * On failure, ptr is set to \a last and ec std::errc::value_too_large.
	 */
	[[nodiscard]] std::to_chars_result to_chars (char *first, char *last) const noexcept;

	/**
	 * Parse human readable IPv6 address from range [\a first, \a last).
	 * Accepts optional %N suffix for scope identifier.
	 *
	 * On success, returns std::from_chars_result with ptr pointing to
	 * first character not matching the address text.
	 *
	 * On failure, ptr is set to \a first and ec std::errc::invalid_argument.
	 */
	[[nodiscard]] std::from_chars_result from_chars (const char *first, const char *last) noexcept;

	/// Return true if \a this is unspecified address (::)
	[[nodiscard]] constexpr bool is_unspecified () const noexcept
	{
		return to_bytes() == any.to_bytes();
	}

	/// Return true if \a this is loopback address (::1)
	[[nodiscard]] constexpr bool is_loopback () const noexcept
	{
		return to_bytes() == loopback.to_bytes();
	}

	/// Return true if \a this represents IPv4-mapped IPv6 address (::ffff:0:0/96)
	[[nodiscard]] constexpr bool is_v4_mapped () const noexcept
	{
		constexpr std::array<uint8_t, 12> prefix{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff};
		return std::equal(prefix.begin(), prefix.end(), bytes_.begin());
	}

	/// Return true if \a this is a link-local address (fe80::/10)
	[[nodiscard]] constexpr bool is_link_local () const noexcept
	{
		return bytes_[0] == 0xfe && (bytes_[1] & 0xc0) == 0x80;
	}

	/// Return true if \a this is a site-local address (fec0::/10, deprecated)
	[[nodiscard]] constexpr bool is_site_local () const noexcept
	{
		return bytes_[0] == 0xfe && (bytes_[1] & 0xc0) == 0xc0;
	}

	/// Return true if \a this is a multicast address (ff00::/8)
	[[nodiscard]] constexpr bool is_multicast () const noexcept
	{
		return bytes_[0] == 0xff;
	}

	/// Return true if \a this is a node-local multicast address (ff01::/16)
	[[nodiscard]] constexpr bool is_multicast_node_local () const noexcept
	{
		return is_multicast() && (bytes_[1] & 0x0f) == 0x01;
	}

	/// Return true if \a this is a link-local multicast address (ff02::/16)
	[[nodiscard]] constexpr bool is_multicast_link_local () const noexcept
	{
		return is_multicast() && (bytes_[1] & 0x0f) == 0x02;
	}

	/// Return true if \a this is a site-local multicast address (ff05::/16)
	[[nodiscard]] constexpr bool is_multicast_site_local () const noexcept
	{
		return is_multicast() && (bytes_[1] & 0x0f) == 0x05;
	}

	/// Return true if \a this is an org-local multicast address (ff08::/16)
	[[nodiscard]] constexpr bool is_multicast_org_local () const noexcept
	{
		return is_multicast() && (bytes_[1] & 0x0f) == 0x08;
	}

	/// Return true if \a this is a global multicast address (ff0e::/16)
	[[nodiscard]] constexpr bool is_multicast_global () const noexcept
	{
		return is_multicast() && (bytes_[1] & 0x0f) == 0x0e;
	}

	/// Three-way comparison including scope_id (total order).
	/// Deliberate deviation from Networking TS where operator< ignores scope_id.
	constexpr auto operator<=> (const address_v6 &) const noexcept = default;

	/// Return hash value for \a this, incorporating scope_id
	[[nodiscard]] constexpr uint64_t hash () const noexcept
	{
		return hash_128_to_64(fnv_1a_64(bytes_), static_cast<uint64_t>(scope_id_));
	}

	/// Unspecified IPv6 address (::)
	static const address_v6 any;

	/// IPv6 loopback address (::1)
	static const address_v6 loopback;

private:

	bytes_type bytes_;
	scope_id_type scope_id_;
};

// clang-format off
inline constexpr address_v6 address_v6::any{
	address_v6::bytes_type{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
};
inline constexpr address_v6 address_v6::loopback{
	address_v6::bytes_type{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}
};
// clang-format on

/// Return new address from \a bytes with optional \a scope_id
[[nodiscard]] constexpr address_v6
make_address_v6 (const address_v6::bytes_type &bytes, address_v6::scope_id_type scope_id = 0) noexcept
{
	return address_v6{bytes, scope_id};
}

/// Return new address parsed from human readable \a text (may include %scope_id suffix)
[[nodiscard]] inline result<address_v6> make_address_v6 (std::string_view text) noexcept
{
	address_v6 addr;
	auto [_, ec] = addr.from_chars(text.data(), text.data() + text.size());
	if (ec == std::errc{})
	{
		return addr;
	}
	return make_unexpected(ec);
}

/// Tag type to invoke conversion between IPv6 and IPv4-mapped IPv6 addresses.
struct v4_mapped_t
{
};

/// Tag to invoke conversion between IPv6 and IPv4-mapped IPv6 addresses.
constexpr v4_mapped_t v4_mapped;

/// Return new IPv4 address from IPv4-mapped IPv6 address \a a.
/// If \a a has a non-zero scope_id, it is dropped (IPv4 has no zones).
[[nodiscard]] inline result<address_v4> make_address_v4 (v4_mapped_t, const address_v6 &a) noexcept
{
	if (a.is_v4_mapped())
	{
		const auto &v6b = a.to_bytes();
		return address_v4::bytes_type{v6b[12], v6b[13], v6b[14], v6b[15]};
	}
	return make_unexpected(std::errc::invalid_argument);
}

/// Return new IPv4-mapped IPv6 address from IPv4 address \a a.
[[nodiscard]] constexpr address_v6 make_address_v6 (v4_mapped_t, const address_v4 &a) noexcept
{
	const auto &v4b = a.to_bytes();
	return address_v6::bytes_type{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff, v4b[0], v4b[1], v4b[2], v4b[3]};
}

} // namespace pal::net::ip

namespace std
{

template <>
struct hash<pal::net::ip::address_v6>
{
	[[nodiscard]] size_t operator() (const pal::net::ip::address_v6 &a) const noexcept
	{
		return a.hash();
	}
};

template <>
struct formatter<pal::net::ip::address_v6>
{
	template <typename ParseContext>
	static constexpr ParseContext::iterator parse (ParseContext &ctx)
	{
		auto it = ctx.begin();
		if (it != ctx.end() && *it != '}')
		{
			throw std::format_error("invalid format for pal::net::ip::address_v6");
		}
		return it;
	}

	template <typename FormatContext>
	FormatContext::iterator format (const pal::net::ip::address_v6 &a, FormatContext &ctx) const
	{
		std::array<char, pal::net::ip::address_v6::max_string_length + 1> text{};
		auto [end, _] = a.to_chars(text.data(), text.data() + text.size());
		return std::copy(text.data(), end, ctx.out());
	}
};

} // namespace std

namespace pal
{

template <>
struct masked_formatter<net::ip::address_v6>
{
	template <typename FormatContext>
	static FormatContext::iterator format (const net::ip::address_v6 &a, FormatContext &ctx)
	{
		auto bytes = a.to_bytes();
		std::fill(bytes.begin() + 8, bytes.end(), uint8_t{});
		const net::ip::address_v6 masked{bytes};
		std::array<char, net::ip::address_v6::max_string_length + 1> text{};
		auto [end, _] = masked.to_chars(text.data(), text.data() + text.size());
		return std::copy(text.data(), end, ctx.out());
	}
};

} // namespace pal

// NOLINTEND(readability-magic-numbers)
