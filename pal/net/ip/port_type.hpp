#pragma once

/**
 * \file pal/net/ip/port_type.hpp
 * IP port number type
 */

#include <charconv>
#include <concepts>
#include <cstdint>
#include <format>
#include <optional>
#include <string_view>

namespace pal::net::ip
{

#define __pal_net_ip_port_type(Port, Alias) \
	Port(unspecified, 0) \
	Port(ftp_data, 20) \
	Port(ftp, 21) \
	Port(ssh, 22) \
	Port(telnet, 23) \
	Port(dns, 53) \
	Port(http, 80) \
	Port(ntp, 123) \
	Port(ldap, 389) \
	Port(https, 443) \
	Port(ldaps, 636) \
	Port(stun, 3478) \
	Alias(turn, stun) \
	Port(stuns, 5349) \
	Alias(turns, stuns)

/// IP port number in host byte order
enum class port_type: uint16_t
{
	#define __pal_net_ip_port_type_value(Name, Value) Name = (Value),
	#define __pal_net_ip_port_type_alias(Name, Target) Name = (Target),
	__pal_net_ip_port_type(__pal_net_ip_port_type_value, __pal_net_ip_port_type_alias)
	#undef __pal_net_ip_port_type_alias
	#undef __pal_net_ip_port_type_value
};

/// Return string representation of named \a port, or empty if unknown.
/// An alias shares its target's value, so the target's name is returned.
constexpr std::string_view to_string_view (port_type port) noexcept
{
	switch (port)
	{
		#define __pal_net_ip_port_type_value(Name, Value) case port_type::Name: return #Name;
		#define __pal_net_ip_port_type_alias(Name, Target)
		__pal_net_ip_port_type(__pal_net_ip_port_type_value, __pal_net_ip_port_type_alias)
		#undef __pal_net_ip_port_type_alias
		#undef __pal_net_ip_port_type_value
	}
	return {};
}

/// Return port parsed from decimal \a text in range [0, 65535] or named
/// \a text (including aliases), or empty if neither.
[[nodiscard]] constexpr std::optional<port_type> to_port_type (std::string_view text) noexcept
{
	uint16_t value = 0;
	const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
	if (ec == std::errc{} && ptr == text.data() + text.size())
	{
		return port_type{value};
	}

	#define __pal_net_ip_port_type_value(Name, Value) \
	if (text == #Name) \
	{ \
		return port_type::Name; \
	}
	__pal_net_ip_port_type(__pal_net_ip_port_type_value, __pal_net_ip_port_type_value)
	#undef __pal_net_ip_port_type_value

	return std::nullopt;
}

/// Return \a port + \a n
template <std::integral T>
constexpr port_type operator+ (port_type port, T n) noexcept
{
	return port_type{static_cast<uint16_t>(static_cast<uint16_t>(port) + static_cast<uint16_t>(n))};
}

/// Return \a port - \a n
template <std::integral T>
constexpr port_type operator- (port_type port, T n) noexcept
{
	return port_type{static_cast<uint16_t>(static_cast<uint16_t>(port) - static_cast<uint16_t>(n))};
}

/// Pre-increment
constexpr port_type &operator++ (port_type &port) noexcept
{
	return port = port + 1;
}

/// Post-increment
constexpr port_type operator++ (port_type &port, int) noexcept
{
	auto tmp = port;
	++port;
	return tmp;
}

/// Pre-decrement
constexpr port_type &operator-- (port_type &port) noexcept
{
	return port = port - 1;
}

/// Post-decrement
constexpr port_type operator-- (port_type &port, int) noexcept
{
	auto tmp = port;
	--port;
	return tmp;
}

} // namespace pal::net::ip

namespace std
{

template <>
struct formatter<pal::net::ip::port_type>
{
	bool named_ = false;

	template <typename ParseContext>
	constexpr ParseContext::iterator parse (ParseContext &ctx)
	{
		auto it = ctx.begin();
		if (it != ctx.end() && *it == 'n')
		{
			named_ = true;
			++it;
		}
		if (it != ctx.end() && *it != '}')
		{
			throw format_error("invalid format for pal::net::ip::port_type");
		}
		return it;
	}

	template <typename FormatContext>
	FormatContext::iterator format (pal::net::ip::port_type port, FormatContext &ctx) const
	{
		if (named_)
		{
			if (auto sv = pal::net::ip::to_string_view(port); !sv.empty())
			{
				return std::ranges::copy(sv, ctx.out()).out;
			}
		}
		return format_to(ctx.out(), "{}", static_cast<uint16_t>(port));
	}
};

} // namespace std
