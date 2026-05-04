#pragma once

/**
 * \file pal/net/test.hpp
 * Test fixtures and utilities for pal::net tests
 */

#include <pal/buffer.hpp>
#include <pal/net/socket_option.hpp>
#include <pal/net/ip/address_v4.hpp>
#include <pal/net/ip/address_v6.hpp>
#include <pal/net/ip/socket_option.hpp>
#include <pal/net/ip/tcp.hpp>
#include <pal/net/ip/udp.hpp>
#include <limits>
#include <random>
#include <string_view>

namespace pal_test
{

// clang-format off

//
// Protocol fixture types
//

struct udp_v4
{
	static constexpr auto protocol_v = pal::net::ip::udp::v4;

	static auto make_socket () noexcept
	{
		return pal::net::make_datagram_socket(protocol_v);
	}

	static pal::net::ip::udp::endpoint loopback_endpoint () noexcept
	{
		return {pal::net::ip::address_v4::loopback, pal::net::ip::port_type::unspecified};
	}
};

struct udp_v6
{
	static constexpr auto protocol_v = pal::net::ip::udp::v6;

	static auto make_socket () noexcept
	{
		return pal::net::make_datagram_socket(protocol_v);
	}

	static pal::net::ip::udp::endpoint loopback_endpoint () noexcept
	{
		return {pal::net::ip::address_v6::loopback, pal::net::ip::port_type::unspecified};
	}
};

struct tcp_v4
{
	static constexpr auto protocol_v = pal::net::ip::tcp::v4;

	static auto make_socket () noexcept
	{
		return pal::net::make_stream_socket(protocol_v);
	}

	static auto make_acceptor () noexcept
	{
		return pal::net::make_socket_acceptor(protocol_v);
	}

	static pal::net::ip::tcp::endpoint loopback_endpoint () noexcept
	{
		return {pal::net::ip::address_v4::loopback, pal::net::ip::port_type::unspecified};
	}
};

struct tcp_v6
{
	static constexpr auto protocol_v = pal::net::ip::tcp::v6;

	static auto make_socket () noexcept
	{
		return pal::net::make_stream_socket(protocol_v);
	}

	static auto make_acceptor () noexcept
	{
		return pal::net::make_socket_acceptor(protocol_v);
	}

	static pal::net::ip::tcp::endpoint loopback_endpoint () noexcept
	{
		return {pal::net::ip::address_v6::loopback, pal::net::ip::port_type::unspecified};
	}
};

template <typename Base>
struct v6_only_of: Base
{
	static auto make_socket () noexcept
	{
		return Base::make_socket().and_then([] (auto s) -> decltype(Base::make_socket())
		{
			return s.set_option(pal::net::ip::v6_only{true}).transform([&]
			{
				return std::move(s);
			});
		});
	}
};

struct udp_v6_only: v6_only_of<udp_v6>
{
};

struct tcp_v6_only: v6_only_of<tcp_v6>
{
	static auto make_acceptor () noexcept
	{
		return tcp_v6::make_acceptor().and_then([] (auto a) -> decltype(tcp_v6::make_acceptor())
		{
			return a.set_option(pal::net::ip::v6_only{true}).transform([&]
			{
				return std::move(a);
			});
		});
	}
};

struct invalid_protocol
{
	using socket = pal::net::basic_datagram_socket<invalid_protocol>;

	struct endpoint
	{
		using protocol_type = invalid_protocol;

		void *data () noexcept
		{
			return nullptr;
		}
		[[nodiscard]] const void *data () const noexcept
		{
			return nullptr;
		}
		[[nodiscard]] static size_t size () noexcept
		{
			return 0;
		}
		[[nodiscard]] static size_t capacity () noexcept
		{
			return 0;
		}
		static pal::result<void> resize (size_t) noexcept
		{
			return {};
		}
		[[nodiscard]] static invalid_protocol protocol () noexcept
		{
			return {};
		}
		bool operator== (const endpoint &) const noexcept = default;
	};

	constexpr invalid_protocol () noexcept = default;

	constexpr explicit invalid_protocol (int) noexcept
	{
	}

	static constexpr int invalid_value = (std::numeric_limits<int>::max)();

	[[nodiscard]] static constexpr int family () noexcept
	{
		return invalid_value;
	}
	[[nodiscard]] static constexpr int type () noexcept
	{
		return invalid_value;
	}
	[[nodiscard]] static constexpr int protocol () noexcept
	{
		return invalid_value;
	}
};
static_assert(pal::net::protocol<invalid_protocol>);
static_assert(pal::net::endpoint<invalid_protocol::endpoint>);
static_assert(pal::net::acceptable_protocol<invalid_protocol>);

//
// Type traits
//

// NOLINTBEGIN(readability-simplify-boolean-expr)

template <typename T>
constexpr bool is_tcp_v = false
	|| std::is_same_v<T, tcp_v4>
	|| std::is_same_v<T, tcp_v6>
	|| std::is_same_v<T, tcp_v6_only>
;

template <typename T>
constexpr bool is_udp_v = false
	|| std::is_same_v<T, udp_v4>
	|| std::is_same_v<T, udp_v6>
	|| std::is_same_v<T, udp_v6_only>
;

template <typename T>
constexpr bool is_v4_v = false
	|| std::is_same_v<T, tcp_v4>
	|| std::is_same_v<T, udp_v4>
;

template <typename T>
constexpr bool is_v6_v = false
	|| std::is_same_v<T, tcp_v6>
	|| std::is_same_v<T, tcp_v6_only>
	|| std::is_same_v<T, udp_v6>
	|| std::is_same_v<T, udp_v6_only>
;

// NOLINTEND(readability-simplify-boolean-expr)

template <typename T>
bool has_expected_family (const pal::net::ip::address &addr) noexcept
{
	if constexpr (is_v4_v<T>)
	{
		return addr.is_v4();
	}
	else
	{
		return addr.is_v6();
	}
}

//
// Port allocation — random base to avoid TIME_WAIT collisions across runs
//

inline pal::net::ip::port_type base_port ()
{
	std::random_device dev;
	std::default_random_engine eng(dev());
	std::uniform_int_distribution<uint16_t> dist(1024, 65535);
	return pal::net::ip::port_type{dist(eng)};
}

template <typename Protocol>
pal::net::ip::port_type next_port (const Protocol &)
{
	static auto port = base_port();
	return port++;
}

template <typename Socket>
bool bind_next_available_port (Socket &socket, typename Socket::endpoint_type &endpoint)
{
	for (auto tries = 65535 - 1024; tries; --tries)
	{
		endpoint.port(next_port(endpoint.protocol()));
		if (socket.bind(endpoint))
		{
			return true;
		}
	}
	return false;
}

//
// Invalidate socket handle for error-path tests
//

template <typename Socket>
void close_native_handle (const Socket &socket) noexcept
{
	pal::net::native_socket::close(socket.native_socket().handle());
}

//
// I/O test data
//
// send_view is the concatenation of send_bufs elements.
// send_bufs satisfies const_buffer_sequence directly; use pal::buffer() for single-element variants.
//

constexpr std::string_view send_view = "hello, world";
constexpr std::array<std::string_view, 3> send_bufs = {"hello", ", ", "world"};
static_assert(send_bufs[0].size() + send_bufs[1].size() + send_bufs[2].size() == send_view.size());

inline const auto send_too_long = pal::buffer(send_view, send_view, send_view, send_view, send_view);
static_assert(send_too_long.size() > pal::net::socket_base::io_vector_max_size);

inline std::array<char, 1024> recv_buf;

inline const auto recv_too_long = pal::buffer(recv_buf, recv_buf, recv_buf, recv_buf, recv_buf);
static_assert(recv_too_long.size() > pal::net::socket_base::io_vector_max_size);

inline std::string_view recv_view (size_t n) noexcept
{
	return {recv_buf.data(), n};
}

// clang-format on

} // namespace pal_test
