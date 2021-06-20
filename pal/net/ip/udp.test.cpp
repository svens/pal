#include <pal/net/ip/udp>
#include <pal/net/test>
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
	using protocol_t = decltype(TestType::protocol_v);
	using endpoint_t = typename protocol_t::endpoint;

	// send buffers
	static constexpr std::string_view send_view = "hello, world";
	static constexpr std::string_view send_bufs[] = { "hello", ", ", "world" };
	static constexpr std::array send_msg =
	{
		std::span{send_bufs[0]},
		std::span{send_bufs[1]},
		std::span{send_bufs[2]},
	};
	static constexpr std::array send_msg_list_too_long =
	{
		std::span{send_bufs[0]},
		std::span{send_bufs[0]},
		std::span{send_bufs[0]},
		std::span{send_bufs[0]},
		std::span{send_bufs[0]},
	};

	// receive buffers
	char recv_buf[1024];
	std::span<char> recv_msg{recv_buf};
	auto recv_view = [&](size_t size) -> std::string_view
	{
		return {recv_buf, size};
	};
	static std::array recv_msg_list_too_long =
	{
		recv_msg,
		recv_msg,
		recv_msg,
		recv_msg,
		recv_msg,
	};

	// receiver
	auto receiver = TestType::make_socket().value();
	endpoint_t endpoint{TestType::loopback_v, 0};
	REQUIRE(pal_test::bind_next_available_port(receiver, endpoint));

	// sender
	auto sender = TestType::make_socket().value();
	REQUIRE(sender.connect(endpoint));

	SECTION("send and receive")
	{
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
}


} // namespace
