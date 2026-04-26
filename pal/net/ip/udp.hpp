#pragma once

/**
 * \file pal/net/ip/udp.hpp
 * UDP protocol
 */

#include <pal/net/ip/basic_endpoint.hpp>

namespace pal::net::ip
{

/// UDP protocol type
class udp
{
public:

	/// Endpoint type for UDP
	using endpoint = basic_endpoint<udp>;

	/// Construct UDP protocol for address \a family
	constexpr explicit udp (int family) noexcept
		: family_{family}
	{
	}

	/// Return address family
	[[nodiscard]] constexpr int family () const noexcept
	{
		return family_;
	}

	[[nodiscard]] constexpr auto operator<=> (const udp &) const noexcept = default;

	/// IPv4 UDP
	static const udp v4;

	/// IPv6 UDP
	static const udp v6;

private:

	int family_;
};
static_assert(net::protocol<udp>);
static_assert(net::endpoint<udp::endpoint>);

inline constexpr udp udp::v4{AF_INET};
inline constexpr udp udp::v6{AF_INET6};

} // namespace pal::net::ip
