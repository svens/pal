#include <pal/net/ip/udp.hpp>
#include <pal/net/test.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include <chrono>
#include <string_view>

namespace
{

using namespace pal_test;
using namespace std::chrono_literals;
using namespace std::string_view_literals;

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

	std::array<char, 1024> recv_buf{};
	auto recv_view = [&] (size_t n) noexcept
	{
		return std::string_view{recv_buf.data(), n};
	};

	auto receiver = TestType::make_socket().value();
	endpoint_t endpoint = TestType::loopback_endpoint();
	REQUIRE(bind_next_available_port(receiver, endpoint));

	auto sender = TestType::make_socket().value();

	SECTION("send_to / receive_from")
	{
		SECTION("single buffer")
		{
			auto send = sender.send_to(endpoint, "hello"sv).value();

			endpoint.port(pal::net::ip::port_type::unspecified);
			auto recv = receiver.receive_from(endpoint, recv_buf).value();
			REQUIRE(recv == send);
			CHECK(recv_view(recv) == "hello");
			CHECK(endpoint.port() == sender.local_endpoint().value().port());
		}

		SECTION("peek")
		{
			REQUIRE_NOTHROW(sender.send_to(endpoint, "hello"sv).value());

			auto recv = receiver.receive_from(endpoint, receiver.message_peek, recv_buf).value();
			CHECK(recv_view(recv) == "hello");
			CHECK(endpoint.port() == sender.local_endpoint().value().port());
			CHECK(receiver.available().value() > 0);

			recv_buf[0] = '\0';
			recv = receiver.receive_from(endpoint, recv_buf).value();
			CHECK(recv_view(recv) == "hello");
			CHECK(receiver.available().value() == 0);
		}

		SECTION("scatter/gather")
		{
			auto send = sender.send_to(endpoint, "hello"sv, "world"sv).value();

			endpoint.port(pal::net::ip::port_type::unspecified);
			auto recv = receiver.receive_from(endpoint, recv_buf).value();
			REQUIRE(recv == send);
			CHECK(recv_view(recv) == "helloworld");
			CHECK(endpoint.port() == sender.local_endpoint().value().port());
		}

		SECTION("receive timeout")
		{
			REQUIRE_NOTHROW(receiver.set_option(pal::net::receive_timeout{10ms}).value());
			auto recv = receiver.receive_from(endpoint, recv_buf);
			REQUIRE_FALSE(recv);
			CHECK(recv.error() == std::errc::timed_out);
		}

		SECTION("bad file descriptor")
		{
			close_native_handle(sender);
			auto send = sender.send_to(endpoint, "hello"sv);
			REQUIRE_FALSE(send);
			CHECK(send.error() == std::errc::bad_file_descriptor);

			close_native_handle(receiver);
			auto recv = receiver.receive_from(endpoint, recv_buf);
			REQUIRE_FALSE(recv);
			CHECK(recv.error() == std::errc::bad_file_descriptor);
		}
	}

	SECTION("send / receive (connected)")
	{
		REQUIRE_NOTHROW(sender.connect(endpoint).value());

		SECTION("single buffer")
		{
			auto send = sender.send("hello"sv).value();

			auto recv = receiver.receive(recv_buf).value();
			REQUIRE(recv == send);
			CHECK(recv_view(recv) == "hello");
		}

		SECTION("peek")
		{
			REQUIRE_NOTHROW(sender.send("hello"sv).value());

			auto recv = receiver.receive(receiver.message_peek, recv_buf).value();
			CHECK(recv_view(recv) == "hello");
			CHECK(receiver.available().value() > 0);

			recv_buf[0] = '\0';
			recv = receiver.receive(recv_buf).value();
			CHECK(recv_view(recv) == "hello");
			CHECK(receiver.available().value() == 0);
		}

		SECTION("scatter/gather")
		{
			auto send = sender.send("hello"sv, "world"sv).value();

			auto recv = receiver.receive(recv_buf).value();
			REQUIRE(recv == send);
			CHECK(recv_view(recv) == "helloworld");
		}

		SECTION("send after shutdown")
		{
			REQUIRE_NOTHROW(sender.shutdown(sender.shutdown_send).value());
			auto send = sender.send("hello"sv);
			REQUIRE_FALSE(send);
			CHECK(send.error() == std::errc::not_connected);
		}

		SECTION("receive timeout")
		{
			REQUIRE_NOTHROW(receiver.set_option(pal::net::receive_timeout{10ms}).value());
			auto recv = receiver.receive(recv_buf);
			REQUIRE_FALSE(recv);
			CHECK(recv.error() == std::errc::timed_out);
		}

		SECTION("not connected")
		{
			auto send = receiver.send("hello"sv);
			REQUIRE_FALSE(send);
			CHECK(send.error() == std::errc::not_connected);
		}

		SECTION("bad file descriptor")
		{
			close_native_handle(sender);
			auto send = sender.send("hello"sv);
			REQUIRE_FALSE(send);
			CHECK(send.error() == std::errc::bad_file_descriptor);

			close_native_handle(receiver);
			auto recv = receiver.receive(recv_buf);
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
