#pragma once

/**
 * \file pal/net/ip/tcp.hpp
 * TCP protocol
 */

#include <pal/net/ip/basic_endpoint.hpp>

namespace pal::net::ip
{

/// TCP protocol type
class tcp
{
public:

	/// Endpoint type for TCP
	using endpoint = basic_endpoint<tcp>;

	/// Construct TCP protocol for address \a family
	constexpr explicit tcp (int family) noexcept
		: family_{family}
	{
	}

	/// Return address family
	[[nodiscard]] constexpr int family () const noexcept
	{
		return family_;
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

inline constexpr tcp tcp::v4{AF_INET};
inline constexpr tcp tcp::v6{AF_INET6};

} // namespace pal::net::ip
