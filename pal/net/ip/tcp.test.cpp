#include <pal/net/ip/tcp.hpp>
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

TEST_CASE("net/ip/tcp")
{
	constexpr auto v4 = pal::net::ip::tcp::v4;
	constexpr auto v6 = pal::net::ip::tcp::v6;

	SECTION("constexpr")
	{
		static_assert(v4.family() == AF_INET);
		static_assert(v4.type() == SOCK_STREAM);
		static_assert(v4.protocol() == IPPROTO_TCP);
		static_assert(v4 == pal::net::ip::tcp::v4);
		static_assert(v4 != pal::net::ip::tcp::v6);

		static_assert(v6.family() == AF_INET6);
		static_assert(v6.type() == SOCK_STREAM);
		static_assert(v6.protocol() == IPPROTO_TCP);
		static_assert(v6 != pal::net::ip::tcp::v4);
		static_assert(v6 == pal::net::ip::tcp::v6);
	}
}

TEMPLATE_TEST_CASE("net/ip/tcp", "[!nonportable]", tcp_v4, tcp_v6, tcp_v6_only)
{
	using protocol_t = std::remove_cvref_t<decltype(TestType::protocol_v)>;
	using endpoint_t = protocol_t::endpoint;

	std::array<char, 1024> recv_buf{};
	auto recv_view = [&] (size_t n) noexcept
	{
		return std::string_view{recv_buf.data(), n};
	};

	auto acceptor = TestType::make_acceptor().value();
	endpoint_t endpoint = TestType::loopback_endpoint();
	REQUIRE(bind_next_available_port(acceptor, endpoint));
	REQUIRE_NOTHROW(acceptor.listen().value());

	auto receiver = TestType::make_socket().value();
	REQUIRE_NOTHROW(receiver.connect(endpoint).value());

	auto sender = acceptor.accept().value();

	SECTION("send / receive")
	{
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

		SECTION("receive after shutdown")
		{
			REQUIRE_NOTHROW(receiver.shutdown(receiver.shutdown_receive).value());
			CHECK(receiver.receive(recv_buf).value() == 0);
		}

		SECTION("receive timeout")
		{
			REQUIRE_NOTHROW(receiver.set_option(pal::net::receive_timeout{10ms}).value());
			auto recv = receiver.receive(recv_buf);
			REQUIRE_FALSE(recv);
			CHECK(recv.error() == std::errc::timed_out);
		}

		SECTION("send timeout")
		{
			if constexpr (pal::os == pal::os_type::linux)
			{
				pal::net::receive_buffer_size recv_buf_size{1};
				REQUIRE_NOTHROW(receiver.set_option(recv_buf_size).value());
				REQUIRE_NOTHROW(receiver.get_option(recv_buf_size).value());

				pal::net::send_buffer_size send_buf_size{1};
				REQUIRE_NOTHROW(sender.set_option(send_buf_size).value());
				REQUIRE_NOTHROW(sender.get_option(send_buf_size).value());

				CAPTURE(send_buf_size.value() + recv_buf_size.value());
				const std::string data(send_buf_size.value() + recv_buf_size.value(), 'X');
				REQUIRE(sender.send(data));
				REQUIRE(sender.send(data));

				REQUIRE_NOTHROW(sender.set_option(pal::net::send_timeout{10ms}).value());
				auto send = sender.send(data);
				REQUIRE_FALSE(send);
				CHECK(send.error() == std::errc::timed_out);
			}
			// macOS and Windows ignore specified buffer sizes
		}

		SECTION("not connected")
		{
			sender = TestType::make_socket().value();

			auto send = sender.send("hello"sv);
			REQUIRE_FALSE(send);
			CHECK(send.error() == std::errc::not_connected);

			auto recv = sender.receive(recv_buf);
			REQUIRE_FALSE(recv);
			CHECK(recv.error() == std::errc::not_connected);
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

	SECTION("make_stream_socket with endpoint")
	{
		SECTION("success")
		{
			endpoint.port(next_port(TestType::protocol_v));
			auto socket = pal::net::make_stream_socket(TestType::protocol_v, endpoint).value();
			CHECK(socket.local_endpoint().value() == endpoint);
		}

		SECTION("address in use")
		{
			auto socket = pal::net::make_stream_socket(TestType::protocol_v, endpoint);
			REQUIRE_FALSE(socket);
			CHECK(socket.error() == std::errc::address_in_use);
		}
	}

	SECTION("make_stream_socket with handle")
	{
		auto handle = sender.release().release();
		auto s = pal::net::make_stream_socket(TestType::protocol_v, handle).value();
		CHECK(s.native_socket().handle() == handle);
		CHECK(s.protocol() == TestType::protocol_v);
	}
}

TEMPLATE_TEST_CASE("net/ip/tcp", "", invalid_protocol)
{
	SECTION("make_stream_socket")
	{
		auto s = pal::net::make_stream_socket(TestType{});
		REQUIRE_FALSE(s);
		CHECK(s.error() == std::errc::protocol_not_supported);
	}

	SECTION("make_stream_socket with endpoint")
	{
		auto s = pal::net::make_stream_socket(TestType{}, typename TestType::endpoint{});
		REQUIRE_FALSE(s);
		CHECK(s.error() == std::errc::protocol_not_supported);
	}
}

} // namespace
