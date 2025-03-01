#include <pal/async/datagram_socket>
#include <pal/async/event_loop>
#include <pal/net/ip/udp>
#include <pal/net/test>
#include <catch2/catch_template_test_macros.hpp>
#include <thread>

namespace {

using namespace pal_test;
using namespace std::chrono_literals;

template <typename Socket>
void wait_socket_state_change (Socket &)
{
	if constexpr (pal::os == pal::os_type::macos)
	{
		// It shouldn't be necessary but depending on networking stack,
		// kernel might not mark socket readable immediately after peer
		// sends data
		//
		// So far seen it only with MacOS
		std::this_thread::sleep_for(1ms);
	}
}

TEMPLATE_TEST_CASE("async/datagram_socket", "[!nonportable]",
	udp_v4, udp_v6, udp_v6_only)
{
	auto loop = pal::async::make_loop().value();

	constexpr auto run_duration = 1s;
	constexpr auto small_size = send_view.size() / 2;

	pal::async::task task1, task2, task3;
	pal::net::ip::udp::endpoint endpoint;
	size_t completed = 0, bytes_transferred = 0;

	auto socket = TestType::make_socket().value();
	socket.bind({TestType::loopback_v, 0}).value();
	auto socket_endpoint = socket.local_endpoint().value();
	auto async_socket = loop.make_handle(std::move(socket)).value();

	auto peer = TestType::make_socket().value();
	peer.bind({TestType::loopback_v, 0}).value();
	auto peer_endpoint = peer.local_endpoint().value();

	SECTION("send / async receive_from: single") //{{{1
	{
		bytes_transferred = peer.send_to(send_msg[0], socket_endpoint).value();
		CHECK(bytes_transferred == send_msg[0].size_bytes());

		wait_socket_state_change(async_socket);

		async_socket.receive_from(&task1, recv_msg, endpoint, [&](auto *task, auto &&result)
		{
			completed++;
			REQUIRE(task == &task1);
			REQUIRE(result);
			CHECK(*result == send_msg[0].size_bytes());
			CHECK(recv_view(*result) == send_bufs[0]);
			CHECK(endpoint == peer_endpoint);
		});
		CHECK(completed == 0);

		loop.run_for(run_duration);
		CHECK(completed == 1);
	}

	SECTION("async receive_from / send: single") //{{{1
	{
		async_socket.receive_from(&task1, recv_msg, endpoint, [&](auto *task, auto &&result)
		{
			completed++;
			REQUIRE(task == &task1);
			REQUIRE(result);
			CHECK(*result == send_msg[0].size_bytes());
			CHECK(recv_view(*result) == send_bufs[0]);
			CHECK(endpoint == peer_endpoint);
		});
		CHECK(completed == 0);

		bytes_transferred = peer.send_to(send_msg[0], socket_endpoint).value();
		CHECK(bytes_transferred == send_msg[0].size_bytes());

		wait_socket_state_change(async_socket);

		loop.run_for(run_duration);
		CHECK(completed == 1);
	}

	SECTION("send / async receive_from: vector") //{{{1
	{
		bytes_transferred = peer.send_to(send_msg, socket_endpoint).value();
		CHECK(bytes_transferred == send_view.size());

		wait_socket_state_change(async_socket);

		async_socket.receive_from(&task1, recv_msg, endpoint, [&](auto *task, auto &&result)
		{
			completed++;
			REQUIRE(task == &task1);
			REQUIRE(result);
			CHECK(*result == bytes_transferred);
			CHECK(recv_view(*result) == send_view);
			CHECK(endpoint == peer_endpoint);
		});
		CHECK(completed == 0);

		loop.run_for(run_duration);
		CHECK(completed == 1);
	}

	SECTION("async receive_from / send: vector") //{{{1
	{
		async_socket.receive_from(&task1, recv_msg, endpoint, [&](auto *task, auto &&result)
		{
			completed++;
			REQUIRE(task == &task1);
			REQUIRE(result);
			CHECK(*result == bytes_transferred);
			CHECK(recv_view(*result) == send_view);
			CHECK(endpoint == peer_endpoint);
		});
		CHECK(completed == 0);

		bytes_transferred = peer.send_to(send_msg, socket_endpoint).value();
		CHECK(bytes_transferred == send_view.size());

		wait_socket_state_change(async_socket);

		loop.run_for(run_duration);
		CHECK(completed == 1);
	}

	SECTION("send / async receive_from: buffer too small") //{{{1
	{
		bytes_transferred = peer.send_to(std::span{send_view}, socket_endpoint).value();
		CHECK(bytes_transferred == send_view.size());

		wait_socket_state_change(async_socket);

		async_socket.receive_from(&task1, std::span{recv_buf, small_size}, endpoint, [&](auto *task, auto &&result, auto flags)
		{
			completed++;
			REQUIRE(task == &task1);
			REQUIRE(result);
			CHECK(*result == small_size);
			CHECK(flags == pal::net::socket_base::message_flags::message_truncated);
			CHECK(recv_view(*result) == send_view.substr(0, small_size));
			CHECK(endpoint == peer_endpoint);
		});
		CHECK(completed == 0);

		loop.run_for(run_duration);
		CHECK(completed == 1);
	}

	SECTION("async receive_from / send: buffer too small") //{{{1
	{
		async_socket.receive_from(&task1, std::span{recv_buf, small_size}, endpoint, [&](auto *task, auto &&result, auto flags)
		{
			completed++;
			REQUIRE(task == &task1);
			REQUIRE(result);
			CHECK(*result == small_size);
			CHECK(flags == pal::net::socket_base::message_flags::message_truncated);
			CHECK(recv_view(*result) == send_view.substr(0, small_size));
			CHECK(endpoint == peer_endpoint);
		});
		CHECK(completed == 0);

		bytes_transferred = peer.send_to(std::span{send_view}, socket_endpoint).value();
		CHECK(bytes_transferred == send_view.size());

		wait_socket_state_change(async_socket);

		loop.run_for(run_duration);
		CHECK(completed == 1);
	}

	SECTION("async receive_from: argument list too long") //{{{1
	{
		async_socket.receive_from(&task1, recv_msg_list_too_long, endpoint, [&](auto *task, auto &&result)
		{
			completed++;
			REQUIRE(task == &task1);
			REQUIRE_FALSE(result);
			CHECK(result.error() == std::errc::invalid_argument);
		});
		CHECK(completed == 0);

		loop.run_for(run_duration);
		CHECK(completed == 1);
	}

	SECTION("async receive_from: two requests, one datagram") //{{{1
	{
		async_socket.receive_from(&task1, recv_msg, endpoint, [&](auto *task, auto &&result)
		{
			completed++;
			REQUIRE(task == &task1);
			REQUIRE(result);
			CHECK(*result == bytes_transferred);
			CHECK(recv_view(*result) == send_view);
			CHECK(endpoint == peer_endpoint);
		});
		CHECK(completed == 0);

		std::byte buffer2[1024];
		pal::net::ip::udp::endpoint endpoint2;
		async_socket.receive_from(&task2, std::span{buffer2}, endpoint2, [&](auto *, auto &&)
		{
			FAIL();
		});
		CHECK(completed == 0);

		bytes_transferred = peer.send_to(std::span{send_view}, socket_endpoint).value();
		CHECK(bytes_transferred == send_view.size());

		wait_socket_state_change(async_socket);

		loop.run_for(run_duration);
		CHECK(completed == 1);

		{
			auto unused = std::move(async_socket);
		}
	}

	SECTION("async receive_from: bad file descriptor") //{{{1
	{
		close_native_handle(async_socket.socket());

		async_socket.receive_from(&task1, recv_msg, endpoint, [&](auto *task, auto &&result)
		{
			completed++;
			REQUIRE(task == &task1);
			REQUIRE_FALSE(result);
			CHECK(result.error() == std::errc::bad_file_descriptor);
		});
		CHECK(completed == 0);

		loop.run_for(run_duration);
		CHECK(completed == 1);
	}

	SECTION("async receive_from: operation canceled") //{{{1
	{
		async_socket.receive_from(&task1, recv_msg, endpoint, [&](auto *task, auto &&result)
		{
			completed++;
			REQUIRE(task == &task1);
			REQUIRE_FALSE(result);
			CHECK(result.error() == std::errc::bad_file_descriptor);
		});
		CHECK(completed == 0);

		{
			auto unused = std::move(async_socket);
			std::ignore = unused;
		}

		loop.run_for(run_duration);
		CHECK(completed == 1);
	}

	SECTION("send / async receive: single") //{{{1
	{
		bytes_transferred = peer.send_to(send_msg[0], socket_endpoint).value();
		CHECK(bytes_transferred == send_msg[0].size_bytes());

		async_socket.receive(&task1, recv_msg, [&](auto *task, auto &&result)
		{
			completed++;
			REQUIRE(task == &task1);
			REQUIRE(result);
			CHECK(*result == send_msg[0].size_bytes());
			CHECK(recv_view(*result) == send_bufs[0]);
		});
		CHECK(completed == 0);

		wait_socket_state_change(async_socket);

		loop.run_for(run_duration);
		CHECK(completed == 1);
	}

	SECTION("async receive / send: single") //{{{1
	{
		async_socket.receive(&task1, recv_msg, [&](auto *task, auto &&result)
		{
			completed++;
			REQUIRE(task == &task1);
			REQUIRE(result);
			CHECK(*result == send_msg[0].size_bytes());
			CHECK(recv_view(*result) == send_bufs[0]);
		});
		CHECK(completed == 0);

		bytes_transferred = peer.send_to(send_msg[0], socket_endpoint).value();
		CHECK(bytes_transferred == send_msg[0].size_bytes());

		wait_socket_state_change(async_socket);

		loop.run_for(run_duration);
		CHECK(completed == 1);
	}

	SECTION("send / async receive: vector") //{{{1
	{
		bytes_transferred = peer.send_to(send_msg, socket_endpoint).value();
		CHECK(bytes_transferred == send_view.size());

		async_socket.receive(&task1, recv_msg, [&](auto *task, auto &&result)
		{
			completed++;
			REQUIRE(task == &task1);
			REQUIRE(result);
			CHECK(*result == bytes_transferred);
			CHECK(recv_view(*result) == send_view);
		});
		CHECK(completed == 0);

		wait_socket_state_change(async_socket);

		loop.run_for(run_duration);
		CHECK(completed == 1);
	}

	SECTION("async receive / send: vector") //{{{1
	{
		async_socket.receive(&task1, recv_msg, [&](auto *task, auto &&result)
		{
			completed++;
			REQUIRE(task == &task1);
			REQUIRE(result);
			CHECK(*result == bytes_transferred);
			CHECK(recv_view(*result) == send_view);
		});
		CHECK(completed == 0);

		bytes_transferred = peer.send_to(send_msg, socket_endpoint).value();
		CHECK(bytes_transferred == send_view.size());

		wait_socket_state_change(async_socket);

		loop.run_for(run_duration);
		CHECK(completed == 1);
	}

	SECTION("send / async receive: buffer too small") //{{{1
	{
		bytes_transferred = peer.send_to(std::span{send_view}, socket_endpoint).value();
		CHECK(bytes_transferred == send_view.size());

		wait_socket_state_change(async_socket);

		async_socket.receive(&task1, std::span{recv_buf, small_size}, [&](auto *task, auto &&result, auto flags)
		{
			completed++;
			REQUIRE(task == &task1);
			REQUIRE(result);
			CHECK(*result == small_size);
			CHECK(flags == pal::net::socket_base::message_flags::message_truncated);
			std::ignore = flags;
			CHECK(recv_view(*result) == send_view.substr(0, small_size));
		});
		CHECK(completed == 0);

		loop.run_for(run_duration);
		CHECK(completed == 1);
	}

	SECTION("async receive / send: buffer too small") //{{{1
	{
		async_socket.receive(&task1, std::span{recv_buf, small_size}, [&](auto *task, auto &&result, auto flags)
		{
			completed++;
			REQUIRE(task == &task1);
			REQUIRE(result);
			CHECK(*result == small_size);
			CHECK(flags == pal::net::socket_base::message_flags::message_truncated);
			std::ignore = flags;
			CHECK(recv_view(*result) == send_view.substr(0, small_size));
		});
		CHECK(completed == 0);

		bytes_transferred = peer.send_to(std::span{send_view}, socket_endpoint).value();
		CHECK(bytes_transferred == send_view.size());

		wait_socket_state_change(async_socket);

		loop.run_for(run_duration);
		CHECK(completed == 1);
	}

	SECTION("async receive: argument list too long") //{{{1
	{
		async_socket.receive(&task1, recv_msg_list_too_long, [&](auto *task, auto &&result)
		{
			completed++;
			REQUIRE(task == &task1);
			REQUIRE_FALSE(result);
			CHECK(result.error() == std::errc::invalid_argument);
		});
		CHECK(completed == 0);

		loop.run_for(run_duration);
		CHECK(completed == 1);
	}

	SECTION("async receive: two requests, one datagram") //{{{1
	{
		async_socket.receive(&task1, recv_msg, [&](auto *task, auto &&result)
		{
			CHECK(completed++ == 0);
			REQUIRE(task == &task1);
			REQUIRE(result);
			CHECK(*result == bytes_transferred);
			CHECK(recv_view(*result) == send_view);
		});
		CHECK(completed == 0);

		std::byte buffer2[1024];
		async_socket.receive(&task2, std::span{buffer2}, [&](auto *, auto &&)
		{
			FAIL();
		});
		CHECK(completed == 0);

		bytes_transferred = peer.send_to(std::span{send_view}, socket_endpoint).value();
		CHECK(bytes_transferred == send_view.size());

		wait_socket_state_change(async_socket);

		loop.run_for(run_duration);
		CHECK(completed == 1);
	}

	SECTION("async receive: bad file descriptor") //{{{1
	{
		close_native_handle(async_socket.socket());

		async_socket.receive(&task1, recv_msg, [&](auto *task, auto &&result)
		{
			completed++;
			REQUIRE(task == &task1);
			REQUIRE_FALSE(result);
			CHECK(result.error() == std::errc::bad_file_descriptor);
		});
		CHECK(completed == 0);

		loop.run_for(run_duration);
		CHECK(completed == 1);
	}

	SECTION("async receive: operation canceled") //{{{1
	{
		async_socket.receive(&task1, recv_msg, [&](auto *task, auto &&result)
		{
			completed++;
			REQUIRE(task == &task1);
			REQUIRE_FALSE(result);
			CHECK(result.error() == std::errc::bad_file_descriptor);
		});
		CHECK(completed == 0);

		{
			auto unused = std::move(async_socket);
			std::ignore = unused;
		}

		loop.run_for(run_duration);
		CHECK(completed == 1);
	}

	SECTION("async send_to / recv: single") //{{{1
	{
		async_socket.send_to(&task1, send_msg[0], peer_endpoint, [&](auto *task, auto &&result)
		{
			completed++;
			REQUIRE(task == &task1);
			REQUIRE(result);
			CHECK(*result == send_msg[0].size_bytes());
		});
		CHECK(completed == 0);

		bytes_transferred = peer.receive_from(recv_msg, endpoint).value();
		CHECK(bytes_transferred == send_msg[0].size_bytes());
		CHECK(recv_view(bytes_transferred) == send_bufs[0]);
		CHECK(endpoint == socket_endpoint);

		loop.run_for(run_duration);
		CHECK(completed == 1);
	}

	SECTION("async send_to / recv: vector") //{{{1
	{
		async_socket.send_to(&task1, send_msg, peer_endpoint, [&](auto *task, auto &&result)
		{
			completed++;
			REQUIRE(task == &task1);
			REQUIRE(result);
			CHECK(*result == send_view.size());
		});
		CHECK(completed == 0);

		bytes_transferred = peer.receive_from(recv_msg, endpoint).value();
		CHECK(bytes_transferred == send_view.size());
		CHECK(recv_view(bytes_transferred) == send_view);
		CHECK(endpoint == socket_endpoint);

		loop.run_for(run_duration);
		CHECK(completed == 1);
	}

	SECTION("async send_to: bad file descriptor") //{{{1
	{
		close_native_handle(async_socket.socket());

		async_socket.send_to(&task1, send_msg, peer_endpoint, [&](auto *task, auto &&result)
		{
			completed++;
			REQUIRE(task == &task1);
			REQUIRE_FALSE(result);
			CHECK(result.error() == std::errc::bad_file_descriptor);
		});
		CHECK(completed == 0);

		loop.run_for(run_duration);
		CHECK(completed == 1);
	}

	SECTION("async send_to: argument list too long") //{{{1
	{
		async_socket.send_to(&task1, send_msg_list_too_long, peer_endpoint, [&](auto *task, auto &&result)
		{
			completed++;
			REQUIRE(task == &task1);
			REQUIRE_FALSE(result);
			CHECK(result.error() == std::errc::invalid_argument);
		});
		CHECK(completed == 0);

		loop.run_for(run_duration);
		CHECK(completed == 1);
	}

	SECTION("async send_to: connected") //{{{1
	{
		async_socket.socket().connect(peer_endpoint);

		async_socket.send_to(&task1, send_msg[0], peer_endpoint, [&](auto *task, auto &&result)
		{
			completed++;
			REQUIRE(task == &task1);
			if constexpr (pal::os == pal::os_type::macos)
			{
				REQUIRE_FALSE(result);
				CHECK(result.error() == std::errc::already_connected);
			}
			else
			{
				REQUIRE(result);
				CHECK(*result == send_msg[0].size_bytes());
			}
		});
		CHECK(completed == 0);

		if constexpr (pal::os != pal::os_type::macos)
		{
			bytes_transferred = peer.receive_from(recv_msg, endpoint).value();
			CHECK(bytes_transferred == send_msg[0].size_bytes());
			CHECK(recv_view(bytes_transferred) == send_bufs[0]);
			CHECK(endpoint == socket_endpoint);
		}

		loop.run_for(run_duration);
		CHECK(completed == 1);
	}

	SECTION("async send / recv: single") //{{{1
	{
		async_socket.socket().connect(peer_endpoint);

		async_socket.send(&task1, send_msg[0], [&](auto *task, auto &&result)
		{
			completed++;
			REQUIRE(task == &task1);
			REQUIRE(result);
			CHECK(*result == send_msg[0].size_bytes());
		});
		CHECK(completed == 0);

		bytes_transferred = peer.receive_from(recv_msg, endpoint).value();
		CHECK(bytes_transferred == send_msg[0].size_bytes());
		CHECK(recv_view(bytes_transferred) == send_bufs[0]);
		CHECK(endpoint == socket_endpoint);

		loop.run_for(run_duration);
		CHECK(completed == 1);
	}

	SECTION("async send / recv: vector") //{{{1
	{
		async_socket.socket().connect(peer_endpoint);

		async_socket.send(&task1, send_msg, [&](auto *task, auto &&result)
		{
			completed++;
			REQUIRE(task == &task1);
			REQUIRE(result);
			CHECK(*result == send_view.size());
		});
		CHECK(completed == 0);

		bytes_transferred = peer.receive_from(recv_msg, endpoint).value();
		CHECK(bytes_transferred == send_view.size());
		CHECK(recv_view(bytes_transferred) == send_view);
		CHECK(endpoint == socket_endpoint);

		loop.run_for(run_duration);
		CHECK(completed == 1);
	}

	SECTION("async send: bad file descriptor") //{{{1
	{
		async_socket.socket().connect(peer_endpoint);
		close_native_handle(async_socket.socket());

		async_socket.send(&task1, send_msg, [&](auto *task, auto &&result)
		{
			completed++;
			REQUIRE(task == &task1);
			REQUIRE_FALSE(result);
			CHECK(result.error() == std::errc::bad_file_descriptor);
		});
		CHECK(completed == 0);

		loop.run_for(run_duration);
		CHECK(completed == 1);
	}

	SECTION("async send: argument list too long") //{{{1
	{
		async_socket.socket().connect(peer_endpoint);

		async_socket.send(&task1, send_msg_list_too_long, [&](auto *task, auto &&result)
		{
			completed++;
			REQUIRE(task == &task1);
			REQUIRE_FALSE(result);
			CHECK(result.error() == std::errc::invalid_argument);
		});
		CHECK(completed == 0);

		loop.run_for(run_duration);
		CHECK(completed == 1);
	}

	SECTION("async send: not connected") //{{{1
	{
		async_socket.send(&task1, send_msg[0], [&](auto *task, auto &&result)
		{
			completed++;
			REQUIRE(task == &task1);
			REQUIRE_FALSE(result);
			CHECK(result.error() == std::errc::not_connected);
		});
		CHECK(completed == 0);

		loop.run_for(run_duration);
		CHECK(completed == 1);
	}

	SECTION("async receive_from: cork") //{{{1
	{
		bytes_transferred = peer.send_to(send_msg[0], socket_endpoint).value();
		CHECK(bytes_transferred == send_msg[0].size_bytes());

		async_socket.cork(pal::net::socket_read);

		async_socket.receive_from(&task1, recv_msg, endpoint, [&](auto *task, auto &&result)
		{
			completed++;
			REQUIRE(task == &task1);
			REQUIRE(result);
			CHECK(*result == send_msg[0].size_bytes());
			CHECK(recv_view(*result) == send_bufs[0]);
			CHECK(endpoint == peer_endpoint);
		});
		CHECK(completed == 0);

		loop.run_once();
		CHECK(completed == 0);

		async_socket.uncork(pal::net::socket_read);
		loop.run_for(run_duration);
		CHECK(completed == 1);
	}

	SECTION("async receive: cork") //{{{1
	{
		bytes_transferred = peer.send_to(send_msg[0], socket_endpoint).value();
		CHECK(bytes_transferred == send_msg[0].size_bytes());

		async_socket.cork(pal::net::socket_read);

		async_socket.receive(&task1, recv_msg, [&](auto *task, auto &&result)
		{
			completed++;
			REQUIRE(task == &task1);
			REQUIRE(result);
			CHECK(*result == send_msg[0].size_bytes());
			CHECK(recv_view(*result) == send_bufs[0]);
		});
		CHECK(completed == 0);

		loop.run_once();
		CHECK(completed == 0);

		async_socket.uncork(pal::net::socket_read);
		loop.run_for(run_duration);
		CHECK(completed == 1);
	}

	SECTION("async send_to: cork") //{{{1
	{
		async_socket.cork(pal::net::socket_write);

		async_socket.send_to(&task1, send_msg[0], peer_endpoint, [&](auto *task, auto &&result)
		{
			completed++;
			REQUIRE(task == &task1);
			REQUIRE(result);
			CHECK(*result == send_msg[0].size_bytes());
		});
		CHECK(completed == 0);

		async_socket.send_to(&task2, send_msg[1], peer_endpoint, [&](auto *task, auto &&result)
		{
			completed++;
			REQUIRE(task == &task2);
			REQUIRE(result);
			CHECK(*result == send_msg[1].size_bytes());
		});
		CHECK(completed == 0);

		loop.run_once();
		CHECK(completed == 0);

		async_socket.uncork(pal::net::socket_write);

		loop.run_for(run_duration);
		CHECK(completed == 2);

		bytes_transferred = peer.receive_from(recv_msg, endpoint).value();
		CHECK(bytes_transferred == send_msg[0].size());
		CHECK(recv_view(bytes_transferred) == send_bufs[0]);
		CHECK(endpoint == socket_endpoint);

		bytes_transferred = peer.receive_from(recv_msg, endpoint).value();
		CHECK(bytes_transferred == send_msg[1].size());
		CHECK(recv_view(bytes_transferred) == send_bufs[1]);
		CHECK(endpoint == socket_endpoint);
	}

	SECTION("async send: cork") //{{{1
	{
		async_socket.socket().connect(peer_endpoint);
		async_socket.cork(pal::net::socket_write);

		async_socket.send(&task1, send_msg[0], [&](auto *task, auto &&result)
		{
			completed++;
			REQUIRE(task == &task1);
			REQUIRE(result);
			CHECK(*result == send_msg[0].size_bytes());
		});
		CHECK(completed == 0);

		async_socket.send(&task2, send_msg[1], [&](auto *task, auto &&result)
		{
			completed++;
			REQUIRE(task == &task2);
			REQUIRE(result);
			CHECK(*result == send_msg[1].size_bytes());
		});
		CHECK(completed == 0);

		loop.run_once();
		CHECK(completed == 0);

		async_socket.uncork(pal::net::socket_write);
		loop.run_for(run_duration);
		CHECK(completed == 2);

		bytes_transferred = peer.receive_from(recv_msg, endpoint).value();
		CHECK(bytes_transferred == send_msg[0].size());
		CHECK(recv_view(bytes_transferred) == send_bufs[0]);
		CHECK(endpoint == socket_endpoint);

		bytes_transferred = peer.receive_from(recv_msg, endpoint).value();
		CHECK(bytes_transferred == send_msg[1].size());
		CHECK(recv_view(bytes_transferred) == send_bufs[1]);
		CHECK(endpoint == socket_endpoint);
	}

	SECTION("async send_to / async send: cork") //{{{1
	{
		async_socket.socket().connect(peer_endpoint);
		async_socket.cork(pal::net::socket_write);

		async_socket.send_to(&task1, send_msg[0], peer_endpoint, [&](auto *task, auto &&result)
		{
			completed++;
			REQUIRE(task == &task1);
			if constexpr (pal::os == pal::os_type::macos)
			{
				REQUIRE_FALSE(result);
				CHECK(result.error() == std::errc::already_connected);
			}
			else
			{
				REQUIRE(result);
				CHECK(*result == send_msg[0].size_bytes());
			}
		});
		CHECK(completed == 0);

		async_socket.send(&task2, send_msg[1], [&](auto *task, auto &&result)
		{
			completed++;
			REQUIRE(task == &task2);
			REQUIRE(result);
			CHECK(*result == send_msg[1].size_bytes());
		});
		CHECK(completed == 0);

		loop.run_once();
		CHECK(completed == 0);

		async_socket.uncork(pal::net::socket_write);
		loop.run_for(run_duration);
		CHECK(completed == 2);

		if constexpr (pal::os != pal::os_type::macos)
		{
			bytes_transferred = peer.receive_from(recv_msg, endpoint).value();
			CHECK(bytes_transferred == send_msg[0].size());
			CHECK(recv_view(bytes_transferred) == send_bufs[0]);
			CHECK(endpoint == socket_endpoint);
		}

		bytes_transferred = peer.receive_from(recv_msg, endpoint).value();
		CHECK(bytes_transferred == send_msg[1].size());
		CHECK(recv_view(bytes_transferred) == send_bufs[1]);
		CHECK(endpoint == socket_endpoint);
	}

	SECTION("async send_to: cork / argument list too long") //{{{1
	{
		async_socket.cork(pal::net::socket_write);

		async_socket.send_to(&task1, send_msg[0], peer_endpoint, [&](auto *task, auto &&result)
		{
			completed++;
			REQUIRE(task == &task1);
			REQUIRE(result);
			CHECK(*result == send_msg[0].size_bytes());
		});
		CHECK(completed == 0);

		async_socket.send_to(&task2, send_msg_list_too_long, peer_endpoint, [&](auto *task, auto &&result)
		{
			completed++;
			REQUIRE(task == &task2);
			REQUIRE_FALSE(result);
			CHECK(result.error() == std::errc::invalid_argument);
		});
		CHECK(completed == 0);

		loop.run_for(run_duration);

		// send_to with send_msg_list_too_long fails immediately, bypassing pending queue
		CHECK(completed == 1);
		completed = 0;

		async_socket.uncork(pal::net::socket_write);
		loop.run_for(run_duration);
		CHECK(completed == 1);

		bytes_transferred = peer.receive_from(recv_msg, endpoint).value();
		CHECK(bytes_transferred == send_msg[0].size());
		CHECK(recv_view(bytes_transferred) == send_bufs[0]);
		CHECK(endpoint == socket_endpoint);
	}

	SECTION("async send: cork / argument list too long") //{{{1
	{
		async_socket.socket().connect(peer_endpoint);
		async_socket.cork(pal::net::socket_write);

		async_socket.send(&task1, send_msg[0], [&](auto *task, auto &&result)
		{
			completed++;
			REQUIRE(task == &task1);
			REQUIRE(result);
			CHECK(*result == send_msg[0].size_bytes());
		});
		CHECK(completed == 0);

		async_socket.send(&task2, send_msg_list_too_long, [&](auto *task, auto &&result)
		{
			completed++;
			REQUIRE(task == &task2);
			REQUIRE_FALSE(result);
			CHECK(result.error() == std::errc::invalid_argument);
		});
		CHECK(completed == 0);

		loop.run_for(run_duration);

		// send with send_msg_list_too_long fails immediately, bypassing pending queue
		CHECK(completed == 1);
		completed = 0;

		async_socket.uncork(pal::net::socket_write);
		loop.run_for(run_duration);
		CHECK(completed == 1);

		bytes_transferred = peer.receive_from(recv_msg, endpoint).value();
		CHECK(bytes_transferred == send_msg[0].size());
		CHECK(recv_view(bytes_transferred) == send_bufs[0]);
		CHECK(endpoint == socket_endpoint);
	}

	SECTION("async send_to / async send: cork / not connected") //{{{1
	{
		async_socket.cork(pal::net::socket_write);

		async_socket.send_to(&task1, send_msg[0], peer_endpoint, [&](auto *task, auto &&result)
		{
			completed++;
			REQUIRE(task == &task1);
			REQUIRE(result);
			CHECK(*result == send_msg[0].size_bytes());
		});
		CHECK(completed == 0);

		async_socket.send(&task2, send_msg[1], [&](auto *task, auto &&result)
		{
			completed++;
			REQUIRE(task == &task2);
			REQUIRE_FALSE(result);
			CHECK(result.error() == std::errc::not_connected);
		});
		CHECK(completed == 0);

		async_socket.send_to(&task3, send_msg[2], peer_endpoint, [&](auto *task, auto &&result)
		{
			completed++;
			REQUIRE(task == &task3);
			REQUIRE(result);
			CHECK(*result == send_msg[2].size_bytes());
		});
		CHECK(completed == 0);

		loop.run_once();
		CHECK(completed == 0);

		async_socket.uncork(pal::net::socket_write);
		loop.run_for(run_duration);
		CHECK(completed == 3);

		bytes_transferred = peer.receive_from(recv_msg, endpoint).value();
		CHECK(bytes_transferred == send_msg[0].size());
		CHECK(recv_view(bytes_transferred) == send_bufs[0]);
		CHECK(endpoint == socket_endpoint);

		bytes_transferred = peer.receive_from(recv_msg, endpoint).value();
		CHECK(bytes_transferred == send_msg[2].size());
		CHECK(recv_view(bytes_transferred) == send_bufs[2]);
		CHECK(endpoint == socket_endpoint);
	}

	//}}}1
}

} // namespace
