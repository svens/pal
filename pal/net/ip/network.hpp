#pragma once

/**
 * \file pal/net/ip/network.hpp
 * Version-independent IP network
 */

#include <pal/net/ip/address.hpp>
#include <pal/net/ip/network_v4.hpp>
#include <pal/net/ip/network_v6.hpp>
#include <variant>

namespace pal::net::ip
{

/// Version-independent IP network holding either network_v4 or network_v6.
/// Default-constructed holds an unspecified IPv4 network (0.0.0.0/0).
class network
{
public:

	/// Maximum human readable representation length
	static constexpr size_t max_string_length = network_v6::max_string_length;

	/// Construct new network holding unspecified IPv4 network
	constexpr network () noexcept
		: net_{network_v4{}}
	{
	}

	/// Construct new network holding \a v4
	constexpr network (const network_v4 &v4) noexcept
		: net_{v4}
	{
	}

	/// Construct new network holding \a v6
	constexpr network (const network_v6 &v6) noexcept
		: net_{v6}
	{
	}

	constexpr network (const network &) noexcept = default;
	network &operator= (const network &) noexcept = default;
	constexpr network (network &&) noexcept = default;
	network &operator= (network &&) noexcept = default;

	/// Return true if \a this holds an IPv4 network
	[[nodiscard]] constexpr bool is_v4 () const noexcept
	{
		return std::holds_alternative<network_v4>(net_);
	}

	/// Return true if \a this holds an IPv6 network
	[[nodiscard]] constexpr bool is_v6 () const noexcept
	{
		return std::holds_alternative<network_v6>(net_);
	}

	/// Return pointer to held IPv4 network, or nullptr if holding IPv6
	[[nodiscard]] constexpr const network_v4 *v4 () const noexcept
	{
		return std::get_if<network_v4>(&net_);
	}

	/// Return pointer to held IPv6 network, or nullptr if holding IPv4
	[[nodiscard]] constexpr const network_v6 *v6 () const noexcept
	{
		return std::get_if<network_v6>(&net_);
	}

	/// Return the prefix length
	[[nodiscard]] constexpr uint8_t prefix_length () const noexcept
	{
		if (const auto *p = v4())
		{
			return p->prefix_length();
		}
		return v6()->prefix_length();
	}

	/// Return true if \a addr belongs to this network (cross-family always returns false)
	[[nodiscard]] constexpr bool contains (const address &addr) const noexcept
	{
		if (const auto *p = v4())
		{
			if (const auto *a = addr.v4())
			{
				return p->contains(*a);
			}
			return false;
		}
		if (const auto *a = addr.v6())
		{
			return v6()->contains(*a);
		}
		return false;
	}

	/// Return true if \a this is a subnet of (or equal to) \a that (cross-family always returns false)
	[[nodiscard]] constexpr bool is_subnet_of (const network &that) const noexcept
	{
		if (const auto *p = v4())
		{
			if (const auto *t = that.v4())
			{
				return p->is_subnet_of(*t);
			}
			return false;
		}
		if (const auto *t = that.v6())
		{
			return v6()->is_subnet_of(*t);
		}
		return false;
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
	[[nodiscard]] std::to_chars_result to_chars (char *first, char *last) const noexcept
	{
		if (const auto *p = v4())
		{
			return p->to_chars(first, last);
		}
		return v6()->to_chars(first, last);
	}

	/**
	 * Parse human readable IP network from range [\a first, \a last).
	 * Tries IPv4 first, then IPv6.
	 *
	 * On success, returns std::from_chars_result with ptr pointing to
	 * first character not matching the network text.
	 *
	 * On failure, ptr is set to first non-legal character and ec std::errc::invalid_argument.
	 * Content of \a this is unspecified on failure.
	 */
	[[nodiscard]] std::from_chars_result from_chars (const char *first, const char *last) noexcept
	{
		auto r = net_.emplace<network_v4>().from_chars(first, last);
		if (r.ec != std::errc{})
		{
			r = net_.emplace<network_v6>().from_chars(first, last);
		}
		return r;
	}

	/// Compare networks. Cross-family ordering places all IPv4 networks before all IPv6 networks.
	constexpr auto operator<=> (const network &) const noexcept = default;

	/// Return hash value for \a this
	[[nodiscard]] constexpr size_t hash () const noexcept
	{
		if (const auto *p = v4())
		{
			return p->hash();
		}
		return v6()->hash();
	}

private:

	std::variant<network_v4, network_v6> net_;
};

/// Return new network parsed from human readable \a text.
/// Tries IPv4 first, then IPv6.
[[nodiscard]] inline result<network> make_network (std::string_view text) noexcept
{
	network n;
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
struct hash<pal::net::ip::network>
{
	[[nodiscard]] size_t operator() (const pal::net::ip::network &n) const noexcept
	{
		return n.hash();
	}
};

template <>
struct formatter<pal::net::ip::network>
{
	template <typename ParseContext>
	static constexpr ParseContext::iterator parse (ParseContext &ctx)
	{
		auto it = ctx.begin();
		if (it != ctx.end() && *it != '}')
		{
			throw std::format_error("invalid format for pal::net::ip::network");
		}
		return it;
	}

	template <typename FormatContext>
	FormatContext::iterator format (const pal::net::ip::network &n, FormatContext &ctx) const
	{
		std::array<char, pal::net::ip::network::max_string_length + 1> text{};
		auto [end, _] = n.to_chars(text.data(), text.data() + text.size());
		return std::copy(text.data(), end, ctx.out());
	}
};

} // namespace std
