#include <pal/net/ip/udp>
#include <pal/net/test>


namespace {


using namespace pal_test;


TEMPLATE_TEST_CASE("net/ip/udp", "", udp_v4, udp_v6)
{
	constexpr auto protocol = protocol_v<TestType>;
	std::error_code error;

	endpoint_t<TestType> endpoint;
	auto bind_endpoint = next_endpoint(protocol);
	CAPTURE(bind_endpoint);

	socket_t<TestType> receiver(protocol), sender(protocol);
	bind_available_port(receiver, bind_endpoint);
	const auto connect_endpoint = to_loopback(bind_endpoint);

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

	SECTION("send_to / receive_from")
	{
		SECTION("single noexcept")
		{
			sender.send_to(send_msg[0], connect_endpoint, error);
			REQUIRE(!error);

			size = receiver.receive_from(recv_msg, endpoint, error);
			REQUIRE(!error);

			CHECK(size == send_buf[0].size());
			CHECK(std::string(recv_buf, size) == send_buf[0]);
			CHECK(endpoint.port() == sender.local_endpoint().port());
		}

		SECTION("single")
		{
			REQUIRE_NOTHROW(sender.send_to(send_msg[0], connect_endpoint));
			REQUIRE_NOTHROW(size = receiver.receive_from(recv_msg, endpoint));

			CHECK(size == send_buf[0].size());
			CHECK(std::string(recv_buf, size) == send_buf[0]);
			CHECK(endpoint.port() == sender.local_endpoint().port());
		}

		SECTION("single noexcept with flags")
		{
			sender.send_to(send_msg[0], connect_endpoint, send_flags, error);
			REQUIRE(!error);

			// 1st with peek
			recv_flags = pal::net::socket_base::message_peek;
			size = receiver.receive_from(recv_msg, endpoint, recv_flags, error);
			REQUIRE(!error);

			CHECK(size == send_buf[0].size());
			CHECK(std::string(recv_buf, size) == send_buf[0]);
			CHECK(endpoint.port() == sender.local_endpoint().port());

			// 2nd with actually draining buffer
			size = receiver.receive_from(recv_msg, endpoint, error);
			REQUIRE(!error);

			CHECK(size == send_buf[0].size());
			CHECK(std::string(recv_buf, size) == send_buf[0]);
			CHECK(endpoint.port() == sender.local_endpoint().port());
		}

		SECTION("multiple noexcept")
		{
			sender.send_to(send_msg, connect_endpoint, error);
			REQUIRE(!error);

			size = receiver.receive_from(recv_msg, endpoint, error);
			REQUIRE(!error);

			CHECK(size == send_msg_size);
			CHECK(std::string(recv_buf, size) == message);
			CHECK(endpoint.port() == sender.local_endpoint().port());
		}

		SECTION("multiple")
		{
			REQUIRE_NOTHROW(sender.send_to(send_msg, connect_endpoint));
			REQUIRE_NOTHROW(size = receiver.receive_from(recv_msg, endpoint));

			CHECK(size == send_msg_size);
			CHECK(std::string(recv_buf, size) == message);
			CHECK(endpoint.port() == sender.local_endpoint().port());
		}

		SECTION("multiple noexcept with flags")
		{
			sender.send_to(send_msg, connect_endpoint, send_flags, error);
			REQUIRE(!error);

			// 1st with peek
			recv_flags = pal::net::socket_base::message_peek;
			size = receiver.receive_from(recv_msg, endpoint, recv_flags, error);
			REQUIRE(!error);

			CHECK(size == send_msg_size);
			CHECK(std::string(recv_buf, size) == message);
			CHECK(endpoint.port() == sender.local_endpoint().port());

			// 2nd with actually draining buffer
			size = receiver.receive_from(recv_msg, endpoint, error);
			REQUIRE(!error);

			CHECK(size == send_msg_size);
			CHECK(std::string(recv_buf, size) == message);
			CHECK(endpoint.port() == sender.local_endpoint().port());
		}

		SECTION("closed")
		{
			sender.close();
			sender.send_to(send_msg[0], connect_endpoint, error);
			CHECK(error == std::errc::bad_file_descriptor);

			CHECK_THROWS_AS(
				sender.send_to(send_msg[0], connect_endpoint),
				std::system_error
			);

			receiver.close();
			receiver.receive_from(recv_msg, endpoint, error);
			CHECK(error == std::errc::bad_file_descriptor);

			CHECK_THROWS_AS(
				receiver.receive_from(recv_msg, endpoint),
				std::system_error
			);
		}
	}

	SECTION("send / receive")
	{
		SECTION("not connected")
		{
			sender.send(send_msg[0], error);
			CHECK(error == std::errc::not_connected);
		}

		REQUIRE_NOTHROW(sender.connect(connect_endpoint));

		SECTION("single noexcept")
		{
			sender.send(send_msg[0], error);
			REQUIRE(!error);

			size = receiver.receive(recv_msg, error);
			REQUIRE(!error);

			CHECK(size == send_buf[0].size());
			CHECK(std::string(recv_buf, size) == send_buf[0]);
		}

		SECTION("single")
		{
			REQUIRE_NOTHROW(sender.send(send_msg[0]));
			REQUIRE_NOTHROW(size = receiver.receive(recv_msg));

			CHECK(size == send_buf[0].size());
			CHECK(std::string(recv_buf, size) == send_buf[0]);
		}

		SECTION("single noexcept with flags")
		{
			sender.send(send_msg[0], send_flags, error);
			REQUIRE(!error);

			// 1st with peek
			recv_flags = pal::net::socket_base::message_peek;
			size = receiver.receive(recv_msg, recv_flags, error);
			REQUIRE(!error);

			CHECK(size == send_buf[0].size());
			CHECK(std::string(recv_buf, size) == send_buf[0]);

			// 2nd with actually draining buffer
			size = receiver.receive(recv_msg, error);
			REQUIRE(!error);

			CHECK(size == send_buf[0].size());
			CHECK(std::string(recv_buf, size) == send_buf[0]);
		}

		SECTION("multiple noexcept")
		{
			sender.send(send_msg, error);
			REQUIRE(!error);

			size = receiver.receive(recv_msg, error);
			REQUIRE(!error);

			CHECK(size == send_msg_size);
			CHECK(std::string(recv_buf, size) == message);
		}

		SECTION("multiple")
		{
			REQUIRE_NOTHROW(sender.send(send_msg));
			REQUIRE_NOTHROW(size = receiver.receive(recv_msg));

			CHECK(size == send_msg_size);
			CHECK(std::string(recv_buf, size) == message);
		}

		SECTION("multiple noexcept with flags")
		{
			sender.send(send_msg, send_flags, error);
			REQUIRE(!error);

			// 1st with peek
			recv_flags = pal::net::socket_base::message_peek;
			size = receiver.receive(recv_msg, recv_flags, error);
			REQUIRE(!error);

			CHECK(size == send_msg_size);
			CHECK(std::string(recv_buf, size) == message);

			// 2nd with actually draining buffer
			size = receiver.receive(recv_msg, error);
			REQUIRE(!error);

			CHECK(size == send_msg_size);
			CHECK(std::string(recv_buf, size) == message);
		}

		SECTION("closed")
		{
			sender.close();
			sender.send(send_msg[0], error);
			CHECK(error == std::errc::bad_file_descriptor);

			CHECK_THROWS_AS(
				sender.send(send_msg[0]),
				std::system_error
			);

			receiver.close();
			receiver.receive(recv_msg, error);
			CHECK(error == std::errc::bad_file_descriptor);

			CHECK_THROWS_AS(
				receiver.receive(recv_msg),
				std::system_error
			);
		}
	}
}


} // namespace
