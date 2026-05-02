#include <pal/net/ip/udp.hpp>
#include <pal/net/test.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include <chrono>

namespace
{

using namespace pal_test;
using namespace std::chrono_literals;

TEST_CASE("net/ip/udp")
{
	constexpr auto v4 = pal::net::ip::udp::v4;
	constexpr auto v6 = pal::net::ip::udp::v6;

	SECTION("constexpr")
	{
		static_assert(v4.family() == AF_INET);
		static_assert(v4.type() == SOCK_DGRAM);
		static_assert(v4.protocol() == IPPROTO_UDP);
		static_assert(v4 == pal::net::ip::udp::v4);
		static_assert(v4 != pal::net::ip::udp::v6);

		static_assert(v6.family() == AF_INET6);
		static_assert(v6.type() == SOCK_DGRAM);
		static_assert(v6.protocol() == IPPROTO_UDP);
		static_assert(v6 != pal::net::ip::udp::v4);
		static_assert(v6 == pal::net::ip::udp::v6);
	}
}

TEMPLATE_TEST_CASE("net/ip/udp", "", udp_v4, udp_v6)
{
	using protocol_t = std::remove_cvref_t<decltype(TestType::protocol_v)>;
	using endpoint_t = protocol_t::endpoint;

	auto receiver = TestType::make_socket().value();
	endpoint_t endpoint = TestType::loopback_endpoint();
	REQUIRE(bind_next_available_port(receiver, endpoint));

	auto sender = TestType::make_socket().value();

	SECTION("send_to / receive_from")
	{
		SECTION("single buffer")
		{
			auto send = sender.send_to(pal::buffer(send_bufs[0]), endpoint).value();
			CHECK(send == send_bufs[0].size());

			endpoint.port(pal::net::ip::port_type::unspecified);
			auto recv = receiver.receive_from(pal::buffer(recv_buf), endpoint).value();
			CHECK(recv == send);
			CHECK(recv_view(recv) == send_bufs[0]);
			CHECK(endpoint.port() == sender.local_endpoint().value().port());
		}

		SECTION("peek")
		{
			REQUIRE_NOTHROW(sender.send_to(pal::buffer(send_bufs[0]), endpoint).value());

			auto bufs = pal::buffer(recv_buf);
			auto recv = receiver.receive_from(bufs, endpoint, receiver.message_peek).value();
			CHECK(recv_view(recv) == send_bufs[0]);
			CHECK(endpoint.port() == sender.local_endpoint().value().port());
			CHECK(receiver.available().value() > 0);

			recv_buf[0] = '\0';
			recv = receiver.receive_from(pal::buffer(recv_buf), endpoint).value();
			CHECK(recv_view(recv) == send_bufs[0]);
			CHECK(receiver.available().value() == 0);
		}

		SECTION("scatter/gather")
		{
			auto bufs = pal::buffer(send_bufs[0], send_bufs[1], send_bufs[2]);
			auto send = sender.send_to(bufs, endpoint).value();
			CHECK(send == send_view.size());

			endpoint.port(pal::net::ip::port_type::unspecified);
			auto recv = receiver.receive_from(pal::buffer(recv_buf), endpoint).value();
			CHECK(recv == send);
			CHECK(recv_view(recv) == send_view);
			CHECK(endpoint.port() == sender.local_endpoint().value().port());
		}

		SECTION("argument list too long")
		{
			auto send = sender.send_to(send_too_long, endpoint);
			REQUIRE_FALSE(send);
			CHECK(send.error() == std::errc::argument_list_too_long);

			auto recv = receiver.receive_from(recv_too_long, endpoint);
			REQUIRE_FALSE(recv);
			CHECK(recv.error() == std::errc::argument_list_too_long);
		}

		SECTION("receive timeout")
		{
			REQUIRE_NOTHROW(receiver.set_option(pal::net::receive_timeout{10ms}).value());
			auto recv = receiver.receive_from(pal::buffer(recv_buf), endpoint);
			REQUIRE_FALSE(recv);
			CHECK(recv.error() == std::errc::timed_out);
		}

		SECTION("bad file descriptor")
		{
			close_native_handle(sender);
			auto send = sender.send_to(pal::buffer(send_bufs[0]), endpoint);
			REQUIRE_FALSE(send);
			CHECK(send.error() == std::errc::bad_file_descriptor);

			close_native_handle(receiver);
			auto recv = receiver.receive_from(pal::buffer(recv_buf), endpoint);
			REQUIRE_FALSE(recv);
			CHECK(recv.error() == std::errc::bad_file_descriptor);
		}
	}

	SECTION("send / receive (connected)")
	{
		REQUIRE_NOTHROW(sender.connect(endpoint).value());

		SECTION("single buffer")
		{
			auto send = sender.send(pal::buffer(send_bufs[0])).value();
			CHECK(send == send_bufs[0].size());

			auto recv = receiver.receive(pal::buffer(recv_buf)).value();
			CHECK(recv == send);
			CHECK(recv_view(recv) == send_bufs[0]);
		}

		SECTION("peek")
		{
			REQUIRE_NOTHROW(sender.send(pal::buffer(send_bufs[0])).value());

			auto recv = receiver.receive(pal::buffer(recv_buf), receiver.message_peek).value();
			CHECK(recv_view(recv) == send_bufs[0]);
			CHECK(receiver.available().value() > 0);

			recv_buf[0] = '\0';
			recv = receiver.receive(pal::buffer(recv_buf)).value();
			CHECK(recv_view(recv) == send_bufs[0]);
			CHECK(receiver.available().value() == 0);
		}

		SECTION("scatter/gather")
		{
			auto send = sender.send(pal::buffer(send_bufs[0], send_bufs[1], send_bufs[2])).value();
			CHECK(send == send_view.size());

			auto recv = receiver.receive(pal::buffer(recv_buf)).value();
			CHECK(recv == send);
			CHECK(recv_view(recv) == send_view);
		}

		SECTION("argument list too long")
		{
			auto send = sender.send(send_too_long);
			REQUIRE_FALSE(send);
			CHECK(send.error() == std::errc::argument_list_too_long);

			auto recv = receiver.receive(recv_too_long);
			REQUIRE_FALSE(recv);
			CHECK(recv.error() == std::errc::argument_list_too_long);
		}

		SECTION("send after shutdown")
		{
			REQUIRE_NOTHROW(sender.shutdown(sender.shutdown_send).value());
			auto send = sender.send(pal::buffer(send_bufs[0]));
			REQUIRE_FALSE(send);
			CHECK(send.error() == std::errc::not_connected);
		}

		SECTION("receive timeout")
		{
			REQUIRE_NOTHROW(receiver.set_option(pal::net::receive_timeout{10ms}).value());
			auto recv = receiver.receive(pal::buffer(recv_buf));
			REQUIRE_FALSE(recv);
			CHECK(recv.error() == std::errc::timed_out);
		}

		SECTION("not connected")
		{
			auto send = receiver.send(pal::buffer(send_bufs[0]));
			REQUIRE_FALSE(send);
			CHECK(send.error() == std::errc::not_connected);
		}

		SECTION("bad file descriptor")
		{
			close_native_handle(sender);
			auto send = sender.send(pal::buffer(send_bufs[0]));
			REQUIRE_FALSE(send);
			CHECK(send.error() == std::errc::bad_file_descriptor);

			close_native_handle(receiver);
			auto recv = receiver.receive(pal::buffer(recv_buf));
			REQUIRE_FALSE(recv);
			CHECK(recv.error() == std::errc::bad_file_descriptor);
		}
	}

	SECTION("make_datagram_socket with endpoint")
	{
		SECTION("success")
		{
			std::ignore = receiver.release();
			auto socket = pal::net::make_datagram_socket(TestType::protocol_v, endpoint).value();
			CHECK(socket.local_endpoint().value() == endpoint);
		}

		SECTION("address in use")
		{
			auto socket = pal::net::make_datagram_socket(TestType::protocol_v, endpoint);
			REQUIRE_FALSE(socket);
			CHECK(socket.error() == std::errc::address_in_use);
		}
	}

	SECTION("make_datagram_socket with handle")
	{
		auto handle = sender.release().release();
		auto s = pal::net::make_datagram_socket(TestType::protocol_v, handle).value();
		CHECK(s.native_socket().handle() == handle);
		CHECK(s.protocol() == TestType::protocol_v);
	}
}

TEMPLATE_TEST_CASE("net/ip/udp", "", invalid_protocol)
{
	SECTION("make_datagram_socket")
	{
		auto s = pal::net::make_datagram_socket(TestType{});
		REQUIRE_FALSE(s);
		CHECK(s.error() == std::errc::protocol_not_supported);
	}

	SECTION("make_datagram_socket with endpoint")
	{
		auto s = pal::net::make_datagram_socket(TestType{}, typename TestType::endpoint{});
		REQUIRE_FALSE(s);
		CHECK(s.error() == std::errc::protocol_not_supported);
	}
}

} // namespace
