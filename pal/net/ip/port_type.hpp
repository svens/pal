#pragma once

/**
 * \file pal/net/ip/port_type.hpp
 * IP port number type
 */

#include <concepts>
#include <cstdint>
#include <format>
#include <string_view>

namespace pal::net::ip
{

#define __pal_net_ip_port_type(Impl) \
	Impl(unspecified, 0) \
	Impl(ftp_data, 20) \
	Impl(ftp, 21) \
	Impl(ssh, 22) \
	Impl(telnet, 23) \
	Impl(dns, 53) \
	Impl(http, 80) \
	Impl(ntp, 123) \
	Impl(ldap, 389) \
	Impl(https, 443) \
	Impl(ldaps, 636) \
	Impl(stun, 3478) \
	Impl(stuns, 5349)

// clang-format off

/// IP port number in host byte order
enum class port_type : uint16_t
{
	#define __pal_net_ip_port_type_enum(Name, Value) Name = (Value),
		__pal_net_ip_port_type(__pal_net_ip_port_type_enum)
	#undef __pal_net_ip_port_type_enum
};

/// Return string representation of named \a port, or empty if unknown
constexpr std::string_view to_string_view (port_type port) noexcept
{
	switch (port)
	{
		#define __pal_net_ip_port_type_sv(Name, Value) case port_type::Name: return #Name;
			__pal_net_ip_port_type(__pal_net_ip_port_type_sv)
		#undef __pal_net_ip_port_type_sv
	}
	return {};
}

// clang-format on

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
