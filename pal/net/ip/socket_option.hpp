#pragma once

/**
 * \file pal/net/ip/socket_option.hpp
 * Internet protocol socket options
 */

#include <pal/net/socket_option.hpp>

#if __pal_net_posix
	#include <netinet/in.h>
#elif __pal_net_winsock
	#include <ws2tcpip.h>
#endif

namespace pal::net::ip
{

// clang-format off

/// Determines whether a socket created for an IPv6 protocol is restricted
/// to IPv6 communication only. Initial value depends on the operating system.
using v6_only = socket_option<bool,
	socket_option_level{IPPROTO_IPV6},
	socket_option_name{IPV6_V6ONLY}
>;

// clang-format on

} // namespace pal::net::ip
