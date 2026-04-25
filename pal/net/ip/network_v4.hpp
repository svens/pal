#pragma once

/**
 * \file pal/net/ip/network_v4.hpp
 * IPv4 network
 */

#include <pal/hash.hpp>
#include <pal/net/ip/address_v4.hpp>
#include <pal/result.hpp>
#include <charconv>
#include <compare>
#include <format>
#include <string_view>

namespace pal::net::ip
{

namespace __network_v4
{

[[nodiscard]] constexpr uint32_t make_mask (uint8_t prefix_length) noexcept
{
	static constexpr uint8_t max_prefix_length = 32;
	return prefix_length != 0 ? (~0U << (max_prefix_length - prefix_length)) : 0U;
}

} // namespace __network_v4

/// IPv4 network (address/prefix_length).
/// The network address is always stored in canonical form (host bits zeroed).
class network_v4
{
public:

	/// Maximum prefix length for IPv4
	static constexpr uint8_t max_prefix_length = 32;

	/// Maximum human readable representation length
	static constexpr size_t max_string_length = sizeof("255.255.255.255/32") - 1;

	/// Construct new unspecified network (0.0.0.0/0)
	constexpr network_v4 () noexcept = default;

	/// Return the network address
	[[nodiscard]] constexpr address_v4 address () const noexcept
	{
		return address_;
	}

	/// Return the prefix length (0-32)
	[[nodiscard]] constexpr uint8_t prefix_length () const noexcept
	{
		return prefix_length_;
	}

	/// Return the subnet mask
	[[nodiscard]] constexpr address_v4 netmask () const noexcept
	{
		return address_v4{__network_v4::make_mask(prefix_length_)};
	}

	/// Return the broadcast address
	[[nodiscard]] constexpr address_v4 broadcast () const noexcept
	{
		return address_v4{address_.to_uint() | ~__network_v4::make_mask(prefix_length_)};
	}

	/// Return true if \a this is a host route (prefix_length == 32)
	[[nodiscard]] constexpr bool is_host () const noexcept
	{
		return prefix_length_ == max_prefix_length;
	}

	/// Return true if \a addr belongs to this network
	[[nodiscard]] constexpr bool contains (const address_v4 &addr) const noexcept
	{
		return (addr.to_uint() & __network_v4::make_mask(prefix_length_)) == address_.to_uint();
	}

	/// Return true if \a this is a subnet of (or equal to) \a that
	[[nodiscard]] constexpr bool is_subnet_of (const network_v4 &that) const noexcept
	{
		return prefix_length_ >= that.prefix_length_ && that.contains(address_);
	}

	/**
	 * Convert \a this network to human readable textual representation by
	 * filling range [\a first, \a last).
	 *
	 * On success, returns std::to_chars_result with ptr pointing past
	 * last filled character and ec default value initialized.
	 *
	 * On failure, ptr is set to \a last and ec std::errc::value_too_large.
	 */
	[[nodiscard]] std::to_chars_result to_chars (char *first, char *last) const noexcept;

	/**
	 * Parse human readable IPv4 network from range [\a first, \a last)
	 * (e.g. "192.168.0.0/24"). If no prefix length is specified, defaults to /32.
	 *
	 * On success, returns std::from_chars_result with ptr pointing to
	 * first character not matching the network text.
	 *
	 * On failure, ptr is left at the first character not consumed by parsing and ec std::errc::invalid_argument.
	 * A trailing slash with no valid digits (e.g. "192.168.0.0/") is always an error.
	 */
	[[nodiscard]] std::from_chars_result from_chars (const char *first, const char *last) noexcept;

	constexpr auto operator<=> (const network_v4 &) const noexcept = default;

	/// Return hash value for \a this
	[[nodiscard]] constexpr uint64_t hash () const noexcept
	{
		return hash_128_to_64(address_.hash(), static_cast<uint64_t>(prefix_length_));
	}

private:

	// NOLINTNEXTLINE(readability-redundant-member-init)
	address_v4 address_{};
	uint8_t prefix_length_{};

	constexpr network_v4 (const address_v4 &addr, uint8_t prefix_length) noexcept
		: address_{addr}
		, prefix_length_{prefix_length}
	{
	}

	friend constexpr result<network_v4> make_network_v4 (const address_v4 &, uint8_t) noexcept;
};

/// Return new network from \a addr and \a prefix_length.
/// Returns error if \a prefix_length > 32.
[[nodiscard]] constexpr result<network_v4> make_network_v4 (const address_v4 &addr, uint8_t prefix_length) noexcept
{
	if (prefix_length <= network_v4::max_prefix_length)
	{
		const auto mask = __network_v4::make_mask(prefix_length);
		return network_v4{address_v4{addr.to_uint() & mask}, prefix_length};
	}
	return make_unexpected(std::errc::invalid_argument);
}

/// Return new network parsed from human readable \a text (e.g. "192.168.0.0/24").
/// If no prefix length is specified, defaults to /32 (host route).
[[nodiscard]] inline result<network_v4> make_network_v4 (std::string_view text) noexcept
{
	network_v4 n;
	const auto r = n.from_chars(text.data(), text.data() + text.size());
	if (r.ec == std::errc{} && r.ptr == text.data() + text.size())
	{
		return n;
	}
	return make_unexpected(std::errc::invalid_argument);
}

} // namespace pal::net::ip

namespace std
{

template <>
struct hash<pal::net::ip::network_v4>
{
	[[nodiscard]] size_t operator() (const pal::net::ip::network_v4 &n) const noexcept
	{
		return n.hash();
	}
};

template <>
struct formatter<pal::net::ip::network_v4>
{
	template <typename ParseContext>
	static constexpr ParseContext::iterator parse (ParseContext &ctx)
	{
		auto it = ctx.begin();
		if (it != ctx.end() && *it != '}')
		{
			throw std::format_error("invalid format for pal::net::ip::network_v4");
		}
		return it;
	}

	template <typename FormatContext>
	FormatContext::iterator format (const pal::net::ip::network_v4 &n, FormatContext &ctx) const
	{
		std::array<char, pal::net::ip::network_v4::max_string_length + 1> text{};
		auto [end, _] = n.to_chars(text.data(), text.data() + text.size());
		return std::copy(text.data(), end, ctx.out());
	}
};

} // namespace std
