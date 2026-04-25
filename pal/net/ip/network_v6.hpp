#pragma once

/**
 * \file pal/net/ip/network_v6.hpp
 * IPv6 network
 */

#include <pal/hash.hpp>
#include <pal/net/ip/address_v6.hpp>
#include <pal/result.hpp>
#include <algorithm>
#include <charconv>
#include <ranges>
#include <compare>
#include <format>
#include <string_view>

namespace pal::net::ip
{

// NOLINTBEGIN(readability-magic-numbers)

namespace __network_v6
{

[[nodiscard]] constexpr address_v6::bytes_type make_mask (uint8_t prefix_length) noexcept
{
	address_v6::bytes_type result{};
	for (auto &byte: result)
	{
		if (prefix_length >= 8)
		{
			byte = 0xff;
			prefix_length -= 8;
		}
		else
		{
			byte = static_cast<uint8_t>(prefix_length > 0 ? (0xff << (8 - prefix_length)) : 0);
			prefix_length = 0;
		}
	}
	return result;
}

[[nodiscard]] constexpr address_v6::bytes_type
masked (const address_v6::bytes_type &bytes, uint8_t prefix_length) noexcept
{
	const auto mask = make_mask(prefix_length);
	address_v6::bytes_type result{};
	std::ranges::transform(bytes, mask, result.begin(), std::bit_and<uint8_t>{});
	return result;
}

} // namespace __network_v6

/// IPv6 network (address/prefix_length).
/// The network address is always stored in canonical form (host bits zeroed, no scope_id).
class network_v6
{
public:

	/// Maximum prefix length for IPv6
	static constexpr uint8_t max_prefix_length = 128;

	/// Maximum human readable representation length
	static constexpr size_t max_string_length = address_v6::max_string_length + sizeof("/128") - 1;

	/// Construct new unspecified network (::/0)
	constexpr network_v6 () noexcept = default;

	/// Return the network address
	[[nodiscard]] constexpr address_v6 address () const noexcept
	{
		return address_;
	}

	/// Return the prefix length (0-128)
	[[nodiscard]] constexpr uint8_t prefix_length () const noexcept
	{
		return prefix_length_;
	}

	/// Return the subnet mask
	[[nodiscard]] constexpr address_v6 netmask () const noexcept
	{
		return address_v6{__network_v6::make_mask(prefix_length_)};
	}

	/// Return true if \a this is a host route (prefix_length == 128)
	[[nodiscard]] constexpr bool is_host () const noexcept
	{
		return prefix_length_ == max_prefix_length;
	}

	/// Return true if \a addr belongs to this network
	[[nodiscard]] constexpr bool contains (const address_v6 &addr) const noexcept
	{
		const auto mask = __network_v6::make_mask(prefix_length_);
		return std::ranges::all_of(
			std::views::zip(addr.to_bytes(), mask, address_.to_bytes()),
			[] (auto t)
			{
				auto [b, m, a] = t;
				return (b & m) == a;
			}
		);
	}

	/// Return true if \a this is a subnet of (or equal to) \a that
	[[nodiscard]] constexpr bool is_subnet_of (const network_v6 &that) const noexcept
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
	 * Parse human readable IPv6 network from range [\a first, \a last)
	 * (e.g. "2001:db8::/32"). If no prefix length is specified, defaults to /128.
	 * Any scope_id on the address is silently dropped.
	 *
	 * On success, returns std::from_chars_result with ptr pointing to
	 * first character not matching the network text.
	 *
	 * On failure, ptr is left at the first character not consumed by parsing and ec std::errc::invalid_argument.
	 * A trailing slash with no valid digits (e.g. "::/") is always an error.
	 */
	[[nodiscard]] std::from_chars_result from_chars (const char *first, const char *last) noexcept;

	constexpr auto operator<=> (const network_v6 &) const noexcept = default;

	/// Return hash value for \a this
	[[nodiscard]] constexpr uint64_t hash () const noexcept
	{
		return hash_128_to_64(address_.hash(), static_cast<uint64_t>(prefix_length_));
	}

private:

	// NOLINTNEXTLINE(readability-redundant-member-init)
	address_v6 address_{};
	uint8_t prefix_length_{};

	constexpr network_v6 (const address_v6 &addr, uint8_t prefix_length) noexcept
		: address_{addr}
		, prefix_length_{prefix_length}
	{
	}

	friend constexpr result<network_v6> make_network_v6 (const address_v6 &, uint8_t) noexcept;
};

/// Return new network from \a addr and \a prefix_length.
/// Returns error if \a prefix_length > 128.
[[nodiscard]] constexpr result<network_v6> make_network_v6 (const address_v6 &addr, uint8_t prefix_length) noexcept
{
	if (prefix_length <= network_v6::max_prefix_length)
	{
		return network_v6{address_v6{__network_v6::masked(addr.to_bytes(), prefix_length)}, prefix_length};
	}
	return make_unexpected(std::errc::invalid_argument);
}

/// Return new network parsed from human readable \a text (e.g. "2001:db8::/32").
/// If no prefix length is specified, defaults to /128 (host route).
[[nodiscard]] inline result<network_v6> make_network_v6 (std::string_view text) noexcept
{
	network_v6 n;
	const auto r = n.from_chars(text.data(), text.data() + text.size());
	if (r.ec == std::errc{} && r.ptr == text.data() + text.size())
	{
		return n;
	}
	return make_unexpected(std::errc::invalid_argument);
}

// NOLINTEND(readability-magic-numbers)

} // namespace pal::net::ip

namespace std
{

template <>
struct hash<pal::net::ip::network_v6>
{
	[[nodiscard]] size_t operator() (const pal::net::ip::network_v6 &n) const noexcept
	{
		return n.hash();
	}
};

template <>
struct formatter<pal::net::ip::network_v6>
{
	template <typename ParseContext>
	static constexpr ParseContext::iterator parse (ParseContext &ctx)
	{
		auto it = ctx.begin();
		if (it != ctx.end() && *it != '}')
		{
			throw std::format_error("invalid format for pal::net::ip::network_v6");
		}
		return it;
	}

	template <typename FormatContext>
	FormatContext::iterator format (const pal::net::ip::network_v6 &n, FormatContext &ctx) const
	{
		std::array<char, pal::net::ip::network_v6::max_string_length + 1> text{};
		auto [end, _] = n.to_chars(text.data(), text.data() + text.size());
		return std::copy(text.data(), end, ctx.out());
	}
};

} // namespace std
