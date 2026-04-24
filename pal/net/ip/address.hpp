#pragma once

/**
 * \file pal/net/ip/address.hpp
 * Version-independent IP address
 */

#include <pal/net/ip/address_v6.hpp>
#include <variant>

namespace pal::net::ip
{

/// Version-independent IP address holding either address_v4 or address_v6.
/// Default-constructed holds an unspecified IPv4 address.
class address
{
public:

	/// Maximum human readable representation length
	static constexpr size_t max_string_length = address_v6::max_string_length;

	/// Construct new address holding unspecified IPv4 address
	constexpr address () noexcept
		: addr_{address_v4{}}
	{
	}

	/// Construct new address holding \a v4
	constexpr address (const address_v4 &v4) noexcept
		: addr_{v4}
	{
	}

	/// Construct new address holding \a v6
	constexpr address (const address_v6 &v6) noexcept
		: addr_{v6}
	{
	}

	constexpr address (const address &) noexcept = default;
	address &operator= (const address &) noexcept = default;

	/// Return true if \a this holds an IPv4 address
	[[nodiscard]] constexpr bool is_v4 () const noexcept
	{
		return std::holds_alternative<address_v4>(addr_);
	}

	/// Return true if \a this holds an IPv6 address
	[[nodiscard]] constexpr bool is_v6 () const noexcept
	{
		return std::holds_alternative<address_v6>(addr_);
	}

	/// Return pointer to held IPv4 address, or nullptr if holding IPv6
	[[nodiscard]] constexpr const address_v4 *v4 () const noexcept
	{
		return std::get_if<address_v4>(&addr_);
	}

	/// Return pointer to held IPv6 address, or nullptr if holding IPv4
	[[nodiscard]] constexpr const address_v6 *v6 () const noexcept
	{
		return std::get_if<address_v6>(&addr_);
	}

	/// Return true if \a this is unspecified address
	[[nodiscard]] constexpr bool is_unspecified () const noexcept
	{
		if (const auto *p = v4())
		{
			return p->is_unspecified();
		}
		return v6()->is_unspecified();
	}

	/// Return true if \a this is loopback address
	[[nodiscard]] constexpr bool is_loopback () const noexcept
	{
		if (const auto *p = v4())
		{
			return p->is_loopback();
		}
		return v6()->is_loopback();
	}

	/**
	 * Convert \a this address to human readable textual representation by
	 * filling range [\a first, \a last).
	 *
	 * On success, returns std::to_chars_result with ptr pointing past
	 * last filled character and ec default value initialized.
	 *
	 * On failure, ptr is set to \a last and ec std::errc::value_too_large.
	 */
	[[nodiscard]] std::to_chars_result to_chars (char *first, char *last) const noexcept
	{
		if (const auto *p = v4())
		{
			return p->to_chars(first, last);
		}
		return v6()->to_chars(first, last);
	}

	/**
	 * Parse human readable IP address from range [\a first, \a last).
	 * Tries IPv4 first, then IPv6.
	 *
	 * On success, returns std::from_chars_result with ptr pointing to
	 * first character not matching the address text.
	 *
	 * On failure, ptr is set to \a first and ec std::errc::invalid_argument.
	 * Content of \a this is unspecified on failure.
	 */
	std::from_chars_result from_chars (const char *first, const char *last) noexcept
	{
		auto r = addr_.emplace<address_v4>().from_chars(first, last);
		if (r.ec != std::errc{})
		{
			r = addr_.emplace<address_v6>().from_chars(first, last);
		}
		return r;
	}

	constexpr auto operator<=> (const address &) const noexcept = default;

	/// Return hash value for \a this
	[[nodiscard]] constexpr uint64_t hash () const noexcept
	{
		if (const auto *p = v4())
		{
			return p->hash();
		}
		return v6()->hash();
	}

private:

	std::variant<address_v4, address_v6> addr_;
};

/// Return new address parsed from human readable \a text.
/// Tries IPv4 first, then IPv6.
[[nodiscard]] inline result<address> make_address (std::string_view text) noexcept
{
	address addr;
	auto [_, ec] = addr.from_chars(text.data(), text.data() + text.size());
	if (ec == std::errc{})
	{
		return addr;
	}
	return make_unexpected(ec);
}

} // namespace pal::net::ip

namespace std
{

template <>
struct hash<pal::net::ip::address>
{
	[[nodiscard]] size_t operator() (const pal::net::ip::address &a) const noexcept
	{
		return a.hash();
	}
};

template <>
struct formatter<pal::net::ip::address>
{
	template <typename ParseContext>
	static constexpr ParseContext::iterator parse (ParseContext &ctx)
	{
		auto it = ctx.begin();
		if (it != ctx.end() && *it != '}')
		{
			throw std::format_error("invalid format for pal::net::ip::address");
		}
		return it;
	}

	template <typename FormatContext>
	FormatContext::iterator format (const pal::net::ip::address &a, FormatContext &ctx) const
	{
		std::array<char, pal::net::ip::address::max_string_length + 1> text{};
		auto [end, _] = a.to_chars(text.data(), text.data() + text.size());
		return std::copy(text.data(), end, ctx.out());
	}
};

} // namespace std

namespace pal
{

template <>
struct masked_formatter<net::ip::address>
{
	template <typename FormatContext>
	static FormatContext::iterator format (const net::ip::address &a, FormatContext &ctx)
	{
		if (const auto *v4 = a.v4())
		{
			return masked_formatter<net::ip::address_v4>::format(*v4, ctx);
		}
		return masked_formatter<net::ip::address_v6>::format(*a.v6(), ctx);
	}
};

} // namespace pal
