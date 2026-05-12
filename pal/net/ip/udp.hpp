#pragma once

/**
 * \file pal/net/ip/udp.hpp
 * UDP protocol
 */

#include <pal/net/basic_datagram_socket.hpp>
#include <pal/net/ip/basic_endpoint.hpp>
#include <pal/net/ip/basic_resolver.hpp>

#if __pal_net_posix
	#include <netinet/in.h>
#endif

namespace pal::net::ip
{

/// UDP protocol type
class udp
{
public:

	/// Endpoint type for UDP
	using endpoint = basic_endpoint<udp>;

	/// Datagram socket type for UDP
	using socket = net::basic_datagram_socket<udp>;

	/// Resolver type for UDP
	using resolver = basic_resolver<udp>;

	udp () = delete;

	/// Construct UDP protocol for address \a family
	constexpr explicit udp (int family) noexcept
		: family_{family}
	{
	}

	/// Return address family (domain argument for socket(2))
	[[nodiscard]] constexpr int family () const noexcept
	{
		return family_;
	}

	/// Return socket type (SOCK_DGRAM)
	[[nodiscard]] static constexpr int type () noexcept
	{
		return SOCK_DGRAM;
	}

	/// Return protocol number (IPPROTO_UDP)
	[[nodiscard]] static constexpr int protocol () noexcept
	{
		return IPPROTO_UDP;
	}

	[[nodiscard]] constexpr auto operator<=> (const udp &) const noexcept = default;

	/// IPv4 UDP
	static const udp v4;

	/// IPv6 UDP
	static const udp v6;

private:

	const int family_;
};
static_assert(net::protocol<udp>);
static_assert(net::endpoint<udp::endpoint>);

inline constexpr udp udp::v4{AF_INET};
inline constexpr udp udp::v6{AF_INET6};

} // namespace pal::net::ip
