#pragma once

/**
 * \file pal/net/ip/tcp.hpp
 * TCP protocol
 */

#include <pal/crypto/tls_wire.hpp>
#include <pal/net/basic_socket_acceptor.hpp>
#include <pal/net/basic_stream_socket.hpp>
#include <pal/net/ip/basic_endpoint.hpp>
#include <pal/net/socket_option.hpp>

#if __pal_net_posix
	#include <netinet/in.h>
	#include <netinet/tcp.h>
#elif __pal_net_winsock
	#include <ws2tcpip.h>
#endif

namespace pal::net
{

template <typename Protocol, crypto::transport Transport>
class basic_secure_socket;

} // namespace pal::net

namespace pal::net::ip
{

template <typename Protocol>
class basic_resolver;

/// TCP protocol type
class tcp
{
public:

	/// Endpoint type for TCP
	using endpoint = basic_endpoint<tcp>;

	/// Stream socket type for TCP
	using socket = net::basic_stream_socket<tcp>;

	/// Acceptor type for TCP
	using acceptor = net::basic_socket_acceptor<tcp>;

	/// TLS socket type for TCP
	using secure_socket = net::basic_secure_socket<tcp, crypto::transport::stream>;

	/// Resolver type for TCP
	using resolver = basic_resolver<tcp>;

	// clang-format off

	/// Disable Nagle's algorithm
	using no_delay = socket_option<bool,
		socket_option_level{IPPROTO_TCP},
		socket_option_name{TCP_NODELAY}
	>;

	// clang-format on

	/// Construct TCP protocol for address \a family
	constexpr explicit tcp (int family) noexcept
		: family_{family}
	{
	}

	/// Return address family (domain argument for socket(2))
	[[nodiscard]] constexpr int family () const noexcept
	{
		return family_;
	}

	/// Return socket type (SOCK_STREAM)
	[[nodiscard]] static constexpr int type () noexcept
	{
		return SOCK_STREAM;
	}

	/// Return protocol number (IPPROTO_TCP)
	[[nodiscard]] static constexpr int protocol () noexcept
	{
		return IPPROTO_TCP;
	}

	[[nodiscard]] constexpr auto operator<=> (const tcp &) const noexcept = default;

	/// IPv4 TCP
	static const tcp v4;

	/// IPv6 TCP
	static const tcp v6;

private:

	int family_;
};
static_assert(net::protocol<tcp>);
static_assert(net::endpoint<tcp::endpoint>);
static_assert(net::acceptable_protocol<tcp>);

inline constexpr tcp tcp::v4{AF_INET};
inline constexpr tcp tcp::v6{AF_INET6};

} // namespace pal::net::ip
