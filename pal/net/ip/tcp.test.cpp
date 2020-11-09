#include <pal/net/ip/tcp>
#include <pal/net/test>


namespace {


using namespace pal_test;


template <typename Protocol>
inline auto socket_pair (const Protocol &protocol)
	-> std::pair<typename Protocol::socket, typename Protocol::socket>
{
	auto bind_endpoint = next_endpoint(protocol);
	CAPTURE(bind_endpoint);

	typename Protocol::acceptor acceptor(protocol);
	bind_available_port(acceptor, bind_endpoint);
	acceptor.listen();

	typename Protocol::socket a(protocol);
	a.connect(to_loopback(bind_endpoint));

	return {std::move(a), acceptor.accept()};
}


TEMPLATE_TEST_CASE("net/ip/tcp", "", tcp_v4, tcp_v6)
{
	constexpr auto protocol = protocol_v<TestType>;
	std::error_code error;

	SECTION("no_delay")
	{
		socket_t<TestType> socket(protocol);

		pal::net::ip::tcp::no_delay no_delay;
		socket.get_option(no_delay, error);
		CHECK(!error);
		CHECK(no_delay.value() == false);

		no_delay = true;
		socket.set_option(no_delay, error);
		CHECK(!error);

		no_delay = false;
		socket.get_option(no_delay, error);
		CHECK(!error);
		CHECK(no_delay.value() == true);
	}

	SECTION("not connected")
	{
		socket_t<TestType> socket(protocol);
		socket.send(std::span{"test"}, error);
		CHECK((error == std::errc::not_connected || error == std::errc::broken_pipe));
	}

	SECTION("send / receive")
	{
		const std::string_view send_buf[] = { "hello", ", ", "world" };
		std::array send_msg =
		{
			std::span{send_buf[0]},
			std::span{send_buf[1]},
			std::span{send_buf[2]},
		};
		const auto send_msg_size = pal::span_size_bytes(send_msg);

		std::string message;
		for (const auto &it: send_buf)
		{
			message += it;
		}

		size_t size;
		char recv_buf[1024];
		auto recv_msg = std::span{recv_buf};

		pal::net::socket_base::message_flags recv_flags{}, send_flags{};

		auto [a, b] = socket_pair(protocol);

		SECTION("single noexcept")
		{
			a.send(send_msg[0], error);
			REQUIRE(!error);

			size = b.receive(recv_msg, error);
			REQUIRE(!error);

			CHECK(size == send_buf[0].size());
			CHECK(std::string(recv_buf, size) == send_buf[0]);
		}

		SECTION("single")
		{
			REQUIRE_NOTHROW(a.send(send_msg[0]));
			REQUIRE_NOTHROW(size = b.receive(recv_msg));

			CHECK(size == send_buf[0].size());
			CHECK(std::string(recv_buf, size) == send_buf[0]);
		}

		SECTION("single noexcept with flags")
		{
			a.send(send_msg[0], send_flags, error);
			REQUIRE(!error);

			// 1st with peek
			recv_flags = pal::net::socket_base::message_peek;
			size = b.receive(recv_msg, recv_flags, error);
			REQUIRE(!error);

			CHECK(size == send_buf[0].size());
			CHECK(std::string(recv_buf, size) == send_buf[0]);

			// 2nd with actually draining buffer
			size = b.receive(recv_msg, error);
			REQUIRE(!error);

			CHECK(size == send_buf[0].size());
			CHECK(std::string(recv_buf, size) == send_buf[0]);
		}

		SECTION("multiple noexcept")
		{
			a.send(send_msg, error);
			REQUIRE(!error);

			size = b.receive(recv_msg, error);
			REQUIRE(!error);

			CHECK(size == send_msg_size);
			CHECK(std::string(recv_buf, size) == message);
		}

		SECTION("multiple")
		{
			REQUIRE_NOTHROW(a.send(send_msg));
			REQUIRE_NOTHROW(size = b.receive(recv_msg));

			CHECK(size == send_msg_size);
			CHECK(std::string(recv_buf, size) == message);
		}

		SECTION("multiple noexcept with flags")
		{
			a.send(send_msg, send_flags, error);
			REQUIRE(!error);

			// 1st with peek
			recv_flags = pal::net::socket_base::message_peek;
			size = b.receive(recv_msg, recv_flags, error);
			REQUIRE(!error);

			CHECK(size == send_msg_size);
			CHECK(std::string(recv_buf, size) == message);

			// 2nd with actually draining buffer
			size = b.receive(recv_msg, error);
			REQUIRE(!error);

			CHECK(size == send_msg_size);
			CHECK(std::string(recv_buf, size) == message);
		}

		SECTION("empty")
		{
			std::span<char, 0> buf;
			CHECK(a.send(buf, error) == 0);
			CHECK(!error);

			CHECK(b.receive(buf, error) == 0);
			CHECK(!error);
		}

		SECTION("close")
		{
			a.send(send_msg);
			a.close();
			CHECK(b.receive(recv_msg, error) == send_msg_size);
			CHECK(b.receive(recv_msg, error) == 0);
		}

		SECTION("closed")
		{
			a.close();
			a.send(send_msg, error);
			CHECK(error == std::errc::bad_file_descriptor);

			CHECK_THROWS_AS(
				a.send(send_msg),
				std::system_error
			);

			b.close();
			b.receive(recv_msg, error);
			CHECK(error == std::errc::bad_file_descriptor);

			CHECK_THROWS_AS(
				b.receive(recv_msg),
				std::system_error
			);
		}
	}
}


} // namespace
