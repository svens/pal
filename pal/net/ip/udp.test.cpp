#include <pal/net/ip/udp>
#include <pal/net/test>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include <array>
#include <span>


namespace {


TEST_CASE("net/ip/udp")
{
	constexpr auto v4 = pal::net::ip::udp::v4();
	constexpr auto v6 = pal::net::ip::udp::v6();

	SECTION("constexpr")
	{
		static_assert(v4.family() == AF_INET);
		static_assert(v4.type() == SOCK_DGRAM);
		static_assert(v4.protocol() == IPPROTO_UDP);
		static_assert(v4 == pal::net::ip::udp::v4());
		static_assert(v4 != pal::net::ip::udp::v6());

		static_assert(v6.family() == AF_INET6);
		static_assert(v6.type() == SOCK_DGRAM);
		static_assert(v6.protocol() == IPPROTO_UDP);
		static_assert(v6 == pal::net::ip::udp::v6());
		static_assert(v6 != pal::net::ip::udp::v4());
	}

	SECTION("compare")
	{
		CHECK(v4 == v4);
		CHECK(v4 != v6);
		CHECK(v6 == v6);
		CHECK(v6 != v4);
	}
}


using namespace pal_test;
using namespace std::chrono_literals;


TEMPLATE_TEST_CASE("net/ip/udp", "", udp_v4, udp_v6, udp_v6_only)
{
	using protocol_t = std::remove_cvref_t<decltype(TestType::protocol_v)>;
	using endpoint_t = typename protocol_t::endpoint;

	// receiver
	auto receiver = TestType::make_socket().value();
	endpoint_t endpoint{TestType::loopback_v, 0};
	REQUIRE(pal_test::bind_next_available_port(receiver, endpoint));

	// sender
	auto sender = TestType::make_socket().value();

	SECTION("send_to and receive_from")
	{
		SECTION("single")
		{
			auto send = sender.send_to(send_msg[0], endpoint);
			REQUIRE(send);
			CHECK(*send == send_msg[0].size_bytes());

			endpoint.port(0);
			auto recv = receiver.receive_from(recv_msg, endpoint);
			REQUIRE(recv);
			CHECK(*recv == *send);
			CHECK(recv_view(*recv) == send_bufs[0]);
			CHECK(endpoint.port() == sender.local_endpoint().value().port());
		}

		SECTION("peek")
		{
			auto send = sender.send_to(send_msg[0], endpoint);
			REQUIRE(send);
			CHECK(*send == send_msg[0].size_bytes());

			auto recv = receiver.receive_from(recv_msg, endpoint, receiver.message_peek);
			REQUIRE(recv);
			CHECK(*recv == *send);
			CHECK(recv_view(*recv) == send_bufs[0]);
			CHECK(endpoint.port() == sender.local_endpoint().value().port());
			CHECK(receiver.available().value() > 0);

			recv_buf[0] = '\0';
			recv = receiver.receive_from(recv_msg, endpoint);
			REQUIRE(recv);
			CHECK(*recv == *send);
			CHECK(recv_view(*recv) == send_bufs[0]);
			CHECK(endpoint.port() == sender.local_endpoint().value().port());
			CHECK(receiver.available().value() == 0);
		}

		SECTION("vector")
		{
			auto send = sender.send_to(send_msg, endpoint);
			REQUIRE(send);
			CHECK(*send == send_view.size());

			endpoint.port(0);
			auto recv = receiver.receive_from(recv_msg, endpoint);
			REQUIRE(recv);
			CHECK(*recv == *send);
			CHECK(recv_view(*recv) == send_view);
			CHECK(endpoint.port() == sender.local_endpoint().value().port());
		}

		SECTION("argument list too long")
		{
			auto send = sender.send_to(send_msg_list_too_long, endpoint);
			REQUIRE_FALSE(send);
			CHECK(send.error() == std::errc::argument_list_too_long);

			auto recv = receiver.receive_from(recv_msg_list_too_long, endpoint);
			REQUIRE_FALSE(recv);
			CHECK(recv.error() == std::errc::argument_list_too_long);
		}

		SECTION("receive timeout")
		{
			REQUIRE(receiver.set_option(pal::net::receive_timeout(10ms)));
			auto recv = receiver.receive_from(recv_msg, endpoint);
			REQUIRE_FALSE(recv);
			CHECK(recv.error() == std::errc::timed_out);
		}

		SECTION("bad file descriptor")
		{
			pal_test::handle_guard{sender.native_handle()};
			auto send = sender.send_to(send_msg, endpoint);
			REQUIRE_FALSE(send);
			CHECK(send.error() == std::errc::bad_file_descriptor);

			pal_test::handle_guard{receiver.native_handle()};
			auto recv = receiver.receive_from(recv_msg, endpoint);
			REQUIRE_FALSE(recv);
			CHECK(recv.error() == std::errc::bad_file_descriptor);
		}
	}

	SECTION("send and receive")
	{
		REQUIRE(sender.connect(endpoint));

		SECTION("single")
		{
			auto send = sender.send(send_msg[0]);
			REQUIRE(send);
			CHECK(*send == send_msg[0].size_bytes());

			auto recv = receiver.receive(recv_msg);
			REQUIRE(recv);
			CHECK(*recv == send_msg[0].size_bytes());

			CHECK(recv_view(*recv) == send_bufs[0]);
		}

		SECTION("peek")
		{
			REQUIRE(sender.send(send_msg[0]));

			auto recv = receiver.receive(recv_msg, receiver.message_peek);
			REQUIRE(recv);
			CHECK(*recv == send_msg[0].size_bytes());
			CHECK(recv_view(*recv) == send_bufs[0]);
			CHECK(receiver.available().value() > 0);

			recv_buf[0] = '\0';
			recv = receiver.receive(recv_msg);
			REQUIRE(recv);
			CHECK(*recv == send_msg[0].size_bytes());
			CHECK(recv_view(*recv) == send_bufs[0]);
			CHECK(receiver.available().value() == 0);
		}

		SECTION("vector")
		{
			auto send = sender.send(send_msg);
			REQUIRE(send);
			CHECK(*send == send_view.size());

			auto recv = receiver.receive(recv_msg);
			REQUIRE(recv);
			CHECK(*recv == send_view.size());

			CHECK(recv_view(*recv) == send_view);
		}

		SECTION("send after shutdown")
		{
			auto shutdown = sender.shutdown(sender.shutdown_send);
			REQUIRE(shutdown);

			auto send = sender.send(send_msg);
			REQUIRE_FALSE(send);
			CHECK(send.error() == std::errc::not_connected);
		}

		SECTION("receive after shutdown")
		{
			REQUIRE(receiver.connect(sender.local_endpoint().value()));

			auto shutdown = receiver.shutdown(sender.shutdown_receive);
			REQUIRE(shutdown);

			auto recv = receiver.receive(recv_msg);
			REQUIRE(recv);
			CHECK(*recv == 0);
		}

		SECTION("argument list too long")
		{
			auto send = sender.send(send_msg_list_too_long);
			REQUIRE_FALSE(send);
			CHECK(send.error() == std::errc::argument_list_too_long);

			auto recv = receiver.receive(recv_msg_list_too_long);
			REQUIRE_FALSE(recv);
			CHECK(recv.error() == std::errc::argument_list_too_long);
		}

		SECTION("receive timeout")
		{
			REQUIRE(receiver.set_option(pal::net::receive_timeout(10ms)));
			auto recv = receiver.receive(recv_msg);
			REQUIRE_FALSE(recv);
			CHECK(recv.error() == std::errc::timed_out);
		}

		SECTION("not connected")
		{
			// abuse receiver that is not connected
			auto send = receiver.send(send_msg);
			REQUIRE_FALSE(send);
			CHECK(send.error() == std::errc::not_connected);
		}

		SECTION("bad file descriptor")
		{
			pal_test::handle_guard{sender.native_handle()};
			auto send = sender.send(send_msg);
			REQUIRE_FALSE(send);
			CHECK(send.error() == std::errc::bad_file_descriptor);

			pal_test::handle_guard{receiver.native_handle()};
			auto recv = receiver.receive(recv_msg);
			REQUIRE_FALSE(recv);
			CHECK(recv.error() == std::errc::bad_file_descriptor);
		}
	}

	SECTION("make_datagram_socket with endpoint")
	{
		SECTION("success")
		{
			REQUIRE(receiver.close());
			auto socket = pal::net::make_datagram_socket(TestType::protocol_v, endpoint);
			REQUIRE(socket);
			CHECK(socket->local_endpoint().value() == endpoint);
		}

		SECTION("address in use")
		{
			auto socket = pal::net::make_datagram_socket(TestType::protocol_v, endpoint);
			REQUIRE_FALSE(socket);
			CHECK(socket.error() == std::errc::address_in_use);
		}

		SECTION("not enough memory")
		{
			pal_test::bad_alloc_once x;
			auto socket = pal::net::make_datagram_socket(TestType::protocol_v, endpoint);
			REQUIRE_FALSE(socket);
			CHECK(socket.error() == std::errc::not_enough_memory);
		}
	}

	SECTION("make_datagram_socket with handle")
	{
		pal_test::handle_guard guard{receiver.release()};

		SECTION("success")
		{
			auto socket = pal::net::make_datagram_socket(TestType::protocol_v, guard.handle);
			REQUIRE(socket);
			CHECK(socket->native_handle() == guard.handle);
			guard.release();
		}

		SECTION("not enough memory")
		{
			pal_test::bad_alloc_once x;
			auto socket = pal::net::make_datagram_socket(TestType::protocol_v, guard.handle);
			REQUIRE_FALSE(socket);
			CHECK(socket.error() == std::errc::not_enough_memory);
		}
	}
}


} // namespace
