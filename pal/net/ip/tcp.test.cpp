#include <pal/net/ip/tcp>
#include <pal/net/test>


namespace {


TEST_CASE("net/ip/tcp")
{
	constexpr auto v4 = pal::net::ip::tcp::v4();
	constexpr auto v6 = pal::net::ip::tcp::v6();

	SECTION("constexpr")
	{
		static_assert(v4.family() == AF_INET);
		static_assert(v4.type() == SOCK_STREAM);
		static_assert(v4.protocol() == IPPROTO_TCP);
		static_assert(v4 == pal::net::ip::tcp::v4());
		static_assert(v4 != pal::net::ip::tcp::v6());

		static_assert(v6.family() == AF_INET6);
		static_assert(v6.type() == SOCK_STREAM);
		static_assert(v6.protocol() == IPPROTO_TCP);
		static_assert(v6 == pal::net::ip::tcp::v6());
		static_assert(v6 != pal::net::ip::tcp::v4());
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


TEMPLATE_TEST_CASE("net/ip/tcp", "[!nonportable]", tcp_v4, tcp_v6, tcp_v6_only)
{
	using protocol_t = std::remove_cvref_t<decltype(TestType::protocol_v)>;
	using endpoint_t = typename protocol_t::endpoint;

	auto acceptor = TestType::make_acceptor().value();
	endpoint_t endpoint{TestType::loopback_v, 0};
	REQUIRE(pal_test::bind_next_available_port(acceptor, endpoint));
	REQUIRE(acceptor.listen());

	auto receiver = TestType::make_socket().value();
	REQUIRE(receiver.connect(endpoint));
	auto sender = acceptor.accept().value();

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

		SECTION("send timeout")
		{
			if constexpr (pal::is_linux_build)
			{
				pal::net::receive_buffer_size recv_buf_size{1};
				REQUIRE(receiver.set_option(recv_buf_size));
				REQUIRE(receiver.get_option(recv_buf_size));

				pal::net::send_buffer_size send_buf_size{1};
				REQUIRE(sender.set_option(send_buf_size));
				REQUIRE(sender.get_option(send_buf_size));

				// two sends fill both side buffers
				CAPTURE(send_buf_size.value() + recv_buf_size.value());
				const std::string data(send_buf_size.value() + recv_buf_size.value(), 'X');
				std::span msg{data};
				REQUIRE(sender.send(msg));
				REQUIRE(sender.send(msg));

				// blocks up to send_timeout
				REQUIRE(sender.set_option(pal::net::send_timeout(10ms)));
				auto io = sender.send(msg);
				REQUIRE_FALSE(io);
				CHECK(io.error() == std::errc::timed_out);
			}
			// else: MacOS/Windows ignore specified buffer sizes
		}

		SECTION("not connected")
		{
			sender = TestType::make_socket().value();
			auto io = sender.send(send_msg);
			REQUIRE_FALSE(io);
			CHECK(io.error() == std::errc::not_connected);

			io = sender.receive(recv_msg);
			REQUIRE_FALSE(io);
			CHECK(io.error() == std::errc::not_connected);
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

	SECTION("make_stream_socket with endpoint")
	{
		SECTION("success")
		{
			endpoint.port(pal_test::next_port(TestType::protocol_v));
			auto socket = pal_try(pal::net::make_stream_socket(TestType::protocol_v, endpoint));
			CHECK(socket.local_endpoint().value() == endpoint);
		}

		SECTION("address in use")
		{
			auto socket = pal::net::make_stream_socket(TestType::protocol_v, endpoint);
			REQUIRE_FALSE(socket);
			CHECK(socket.error() == std::errc::address_in_use);
		}

		SECTION("not enough memory")
		{
			pal_test::bad_alloc_once x;
			auto socket = pal::net::make_stream_socket(TestType::protocol_v, endpoint);
			REQUIRE_FALSE(socket);
			CHECK(socket.error() == std::errc::not_enough_memory);
		}
	}

	SECTION("make_stream_socket with handle")
	{
		pal_test::handle_guard guard{receiver.release()};

		SECTION("success")
		{
			auto socket = pal_try(pal::net::make_stream_socket(TestType::protocol_v, guard.handle));
			CHECK(socket.native_handle() == guard.handle);
			guard.release();
		}

		SECTION("not enough memory")
		{
			pal_test::bad_alloc_once x;
			auto socket = pal::net::make_stream_socket(TestType::protocol_v, guard.handle);
			REQUIRE_FALSE(socket);
			CHECK(socket.error() == std::errc::not_enough_memory);
		}
	}
}


} // namespace
