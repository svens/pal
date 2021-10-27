#include <pal/net/ip/udp>
#include <pal/net/async/service>
#include <pal/net/test>
#include <catch2/catch_template_test_macros.hpp>
#include <deque>


namespace {


using namespace pal_test;
using namespace std::chrono_literals;


TEMPLATE_TEST_CASE("net/async/basic_datagram_socket", "[!nonportable]",
	udp_v4, udp_v6, udp_v6_only)
{
	using protocol_t = std::remove_cvref_t<decltype(TestType::protocol_v)>;
	using endpoint_t = typename protocol_t::endpoint;

	pal::net::async::request request{};
	std::deque<pal::net::async::request *> completed{};
	auto add_completed = [&completed](auto *request)
	{
		completed.push_back(request);
	};
	auto service = pal_try(pal::net::async::make_service());

	auto socket = pal_try(TestType::make_socket());
	endpoint_t endpoint{TestType::loopback_v, 0};
	REQUIRE(pal_test::bind_next_available_port(socket, endpoint));

	auto peer = pal_try(TestType::make_socket());
	REQUIRE(peer.bind({TestType::loopback_v, 0}));
	auto peer_endpoint = pal_try(peer.local_endpoint());

	REQUIRE_FALSE(socket.has_async());
	REQUIRE(service.make_async(socket));
	REQUIRE(socket.has_async());

	constexpr auto small_size = send_view.size() / 2;
	constexpr auto run_duration = 1s;

	SECTION("make_async: multiple times")
	{
		if constexpr (!pal::assert_noexcept)
		{
			CHECK_THROWS_AS(service.make_async(socket), std::logic_error);
		}
	}

	SECTION("not async") //{{{1
	{
		if constexpr (!pal::assert_noexcept)
		{
			SECTION("async_receive_from")
			{
				CHECK_THROWS_AS(
					peer.async_receive_from(&request, recv_msg, endpoint),
					std::logic_error
				);
			}

			SECTION("async_receive")
			{
				CHECK_THROWS_AS(
					peer.async_receive(&request, recv_msg),
					std::logic_error
				);
			}

			SECTION("async_send_to")
			{
				CHECK_THROWS_AS(
					peer.async_send_to(&request, send_msg, endpoint),
					std::logic_error
				);
			}

			SECTION("async_send")
			{
				CHECK_THROWS_AS(
					peer.async_send(&request, send_msg),
					std::logic_error
				);
			}
		}
	}

	SECTION("send / async_receive_from: single") //{{{1
	{
		auto sent = pal_try(peer.send_to(send_msg[0], endpoint));
		CHECK(sent == send_msg[0].size_bytes());

		socket.async_receive_from(&request, recv_msg, endpoint);
		REQUIRE(completed.empty());

		service.run_for(run_duration, add_completed);
		CHECK(completed.at(0) == &request);
		REQUIRE_FALSE(request.error);

		auto *receive_from = std::get_if<pal::net::async::receive_from>(&request);
		REQUIRE(receive_from != nullptr);
		CHECK(receive_from->flags == 0);
		REQUIRE(receive_from->bytes_transferred == sent);
		CHECK(recv_view(receive_from->bytes_transferred) == send_bufs[0]);

		CHECK(endpoint == peer_endpoint);
	}

	SECTION("async_receive_from / send: single") //{{{1
	{
		socket.async_receive_from(&request, recv_msg, endpoint);
		REQUIRE(completed.empty());

		auto sent = pal_try(peer.send_to(send_msg[0], endpoint));
		CHECK(sent == send_msg[0].size_bytes());

		service.run_for(run_duration, add_completed);
		CHECK(completed.at(0) == &request);
		REQUIRE_FALSE(request.error);

		auto *receive_from = std::get_if<pal::net::async::receive_from>(&request);
		REQUIRE(receive_from != nullptr);
		CHECK(receive_from->flags == 0);
		REQUIRE(receive_from->bytes_transferred == sent);
		CHECK(recv_view(receive_from->bytes_transferred) == send_bufs[0]);

		CHECK(endpoint == peer_endpoint);
	}

	SECTION("send / async_receive_from: vector") //{{{1
	{
		auto sent = pal_try(peer.send_to(send_msg, endpoint));
		CHECK(sent == send_view.size());

		socket.async_receive_from(&request, recv_msg, endpoint);
		REQUIRE(completed.empty());

		service.run_for(run_duration, add_completed);
		CHECK(completed.at(0) == &request);
		REQUIRE_FALSE(request.error);

		auto *receive_from = std::get_if<pal::net::async::receive_from>(&request);
		REQUIRE(receive_from != nullptr);
		CHECK(receive_from->flags == 0);
		REQUIRE(receive_from->bytes_transferred == sent);
		CHECK(recv_view(receive_from->bytes_transferred) == send_view);

		CHECK(endpoint == peer_endpoint);
	}

	SECTION("async_receive_from / send: vector") //{{{1
	{
		socket.async_receive_from(&request, recv_msg, endpoint);
		REQUIRE(completed.empty());

		auto sent = pal_try(peer.send_to(send_msg, endpoint));
		CHECK(sent == send_view.size());

		service.run_for(run_duration, add_completed);
		CHECK(completed.at(0) == &request);
		REQUIRE_FALSE(request.error);

		auto *receive_from = std::get_if<pal::net::async::receive_from>(&request);
		REQUIRE(receive_from != nullptr);
		CHECK(receive_from->flags == 0);
		REQUIRE(receive_from->bytes_transferred == sent);
		CHECK(recv_view(receive_from->bytes_transferred) == send_view);

		CHECK(endpoint == peer_endpoint);
	}

	SECTION("send / async_receive_from: buffer too small") //{{{1
	{
		auto sent = pal_try(peer.send_to(std::span{send_view}, endpoint));
		CHECK(sent == send_view.size());

		socket.async_receive_from(&request, std::span{recv_buf, small_size}, endpoint);
		REQUIRE(completed.empty());

		service.run_for(run_duration, add_completed);
		CHECK(completed.at(0) == &request);
		REQUIRE_FALSE(request.error);

		auto *receive_from = std::get_if<pal::net::async::receive_from>(&request);
		REQUIRE(receive_from != nullptr);
		CHECK(receive_from->flags == socket.message_truncated);
		REQUIRE(receive_from->bytes_transferred == small_size);
		CHECK(recv_view(receive_from->bytes_transferred) == send_view.substr(0, small_size));

		CHECK(endpoint == peer_endpoint);
	}

	SECTION("async_receive_from / send: buffer too small") //{{{1
	{
		socket.async_receive_from(&request, std::span{recv_buf, small_size}, endpoint);
		REQUIRE(completed.empty());

		auto sent = pal_try(peer.send_to(std::span{send_view}, endpoint));
		CHECK(sent == send_view.size());

		service.run_for(run_duration, add_completed);
		CHECK(completed.at(0) == &request);
		REQUIRE_FALSE(request.error);

		auto *receive_from = std::get_if<pal::net::async::receive_from>(&request);
		REQUIRE(receive_from != nullptr);
		CHECK(receive_from->flags == socket.message_truncated);
		REQUIRE(receive_from->bytes_transferred == small_size);
		CHECK(recv_view(receive_from->bytes_transferred) == send_view.substr(0, small_size));

		CHECK(endpoint == peer_endpoint);
	}

	SECTION("async_receive_from: argument list too long") //{{{1
	{
		socket.async_receive_from(&request, recv_msg_list_too_long, endpoint);
		REQUIRE(completed.empty());

		service.run_for(run_duration, add_completed);
		CHECK(completed.at(0) == &request);
		REQUIRE(request.error == std::errc::argument_list_too_long);
		CHECK(std::holds_alternative<pal::net::async::receive_from>(request));
	}

	SECTION("async_receive_from: two requests, one datagram") //{{{1
	{
		char b2[1024];
		endpoint_t s1, s2;
		pal::net::async::request r2;
		socket.async_receive_from(&request, recv_msg, s1);
		socket.async_receive_from(&r2, std::span{b2}, s2);
		REQUIRE(completed.empty());

		auto sent = pal_try(peer.send_to(std::span{send_view}, endpoint));
		CHECK(sent == send_view.size());

		service.run_for(run_duration, add_completed);
		REQUIRE(completed.size() == 1);
		REQUIRE(completed[0] == &request);
		REQUIRE_FALSE(request.error);

		auto *receive_from = std::get_if<pal::net::async::receive_from>(&request);
		REQUIRE(receive_from != nullptr);
		CHECK(receive_from->flags == 0);
		REQUIRE(receive_from->bytes_transferred == sent);
		CHECK(recv_view(receive_from->bytes_transferred) == send_view);

		REQUIRE(socket.close());
		service.run_for(run_duration, add_completed);
		CHECK(completed.at(1) == &r2);
		CHECK(r2.error == std::errc::bad_file_descriptor);
		CHECK(std::holds_alternative<pal::net::async::receive_from>(r2));
	}

	SECTION("async_receive_from: bad file descriptor") //{{{1
	{
		handle_guard{socket.native_handle()};

		socket.async_receive_from(&request, recv_msg, endpoint);
		REQUIRE(completed.empty());

		service.run_for(run_duration, add_completed);
		CHECK(completed.at(0) == &request);
		REQUIRE(request.error == std::errc::bad_file_descriptor);
		CHECK(std::holds_alternative<pal::net::async::receive_from>(request));
	}

	SECTION("async_receive_from: cancel on socket close") //{{{1
	{
		socket.async_receive_from(&request, recv_msg, endpoint);
		REQUIRE(completed.empty());

		REQUIRE(socket.close());

		service.run_for(run_duration, add_completed);
		CHECK(completed.at(0) == &request);
		REQUIRE(request.error == std::errc::bad_file_descriptor);
		CHECK(std::holds_alternative<pal::net::async::receive_from>(request));
	}

	SECTION("send / async_receive: single") //{{{1
	{
		auto sent = pal_try(peer.send_to(send_msg[0], endpoint));
		CHECK(sent == send_msg[0].size_bytes());

		socket.async_receive(&request, recv_msg);
		REQUIRE(completed.empty());

		service.run_for(run_duration, add_completed);
		CHECK(completed.at(0) == &request);
		REQUIRE_FALSE(request.error);

		auto *receive = std::get_if<pal::net::async::receive>(&request);
		REQUIRE(receive != nullptr);
		CHECK(receive->flags == 0);
		REQUIRE(receive->bytes_transferred == sent);
		CHECK(recv_view(receive->bytes_transferred) == send_bufs[0]);
	}

	SECTION("async_receive / send: single") //{{{1
	{
		socket.async_receive(&request, recv_msg);
		REQUIRE(completed.empty());

		auto sent = pal_try(peer.send_to(send_msg[0], endpoint));
		CHECK(sent == send_msg[0].size_bytes());

		service.run_for(run_duration, add_completed);
		CHECK(completed.at(0) == &request);
		REQUIRE_FALSE(request.error);

		auto *receive = std::get_if<pal::net::async::receive>(&request);
		REQUIRE(receive != nullptr);
		CHECK(receive->flags == 0);
		REQUIRE(receive->bytes_transferred == sent);
		CHECK(recv_view(receive->bytes_transferred) == send_bufs[0]);
	}

	SECTION("send / async_receive: vector") //{{{1
	{
		auto sent = pal_try(peer.send_to(send_msg, endpoint));
		CHECK(sent == send_view.size());

		socket.async_receive(&request, recv_msg);
		REQUIRE(completed.empty());

		service.run_for(run_duration, add_completed);
		CHECK(completed.at(0) == &request);
		REQUIRE_FALSE(request.error);

		auto *receive = std::get_if<pal::net::async::receive>(&request);
		REQUIRE(receive != nullptr);
		CHECK(receive->flags == 0);
		REQUIRE(receive->bytes_transferred == sent);
		CHECK(recv_view(receive->bytes_transferred) == send_view);
	}

	SECTION("async_receive / send: vector") //{{{1
	{
		socket.async_receive(&request, recv_msg);
		REQUIRE(completed.empty());

		auto sent = pal_try(peer.send_to(send_msg, endpoint));
		CHECK(sent == send_view.size());

		service.run_for(run_duration, add_completed);
		CHECK(completed.at(0) == &request);
		REQUIRE_FALSE(request.error);

		auto *receive = std::get_if<pal::net::async::receive>(&request);
		REQUIRE(receive != nullptr);
		CHECK(receive->flags == 0);
		REQUIRE(receive->bytes_transferred == sent);
		CHECK(recv_view(receive->bytes_transferred) == send_view);
	}

	SECTION("send / async_receive: buffer too small") //{{{1
	{
		auto sent = pal_try(peer.send_to(std::span{send_view}, endpoint));
		CHECK(sent == send_view.size());

		socket.async_receive(&request, std::span{recv_buf, small_size});
		REQUIRE(completed.empty());

		service.run_for(run_duration, add_completed);
		CHECK(completed.at(0) == &request);
		REQUIRE_FALSE(request.error);

		auto *receive = std::get_if<pal::net::async::receive>(&request);
		REQUIRE(receive != nullptr);
		CHECK(receive->flags == socket.message_truncated);
		REQUIRE(receive->bytes_transferred == small_size);
		CHECK(recv_view(receive->bytes_transferred) == send_view.substr(0, small_size));
	}

	SECTION("async_receive / send: buffer too small") //{{{1
	{
		socket.async_receive(&request, std::span{recv_buf, small_size});
		REQUIRE(completed.empty());

		auto sent = pal_try(peer.send_to(std::span{send_view}, endpoint));
		CHECK(sent == send_view.size());

		service.run_for(run_duration, add_completed);
		CHECK(completed.at(0) == &request);
		REQUIRE_FALSE(request.error);

		auto *receive = std::get_if<pal::net::async::receive>(&request);
		REQUIRE(receive != nullptr);
		CHECK(receive->flags == socket.message_truncated);
		REQUIRE(receive->bytes_transferred == small_size);
		CHECK(recv_view(receive->bytes_transferred) == send_view.substr(0, small_size));
	}

	SECTION("async_receive: argument list too long") //{{{1
	{
		socket.async_receive(&request, recv_msg_list_too_long);
		REQUIRE(completed.empty());

		service.run_for(run_duration, add_completed);
		CHECK(completed.at(0) == &request);
		REQUIRE(request.error == std::errc::argument_list_too_long);
		CHECK(std::holds_alternative<pal::net::async::receive>(request));
	}

	SECTION("async_receive: two requests, one datagram") //{{{1
	{
		char b2[1024];
		pal::net::async::request r2;
		socket.async_receive(&request, recv_msg);
		socket.async_receive(&r2, std::span{b2});
		REQUIRE(completed.empty());

		auto sent = peer.send_to(std::span{send_view}, endpoint);
		CHECK(sent == send_view.size());

		service.run_for(run_duration, add_completed);
		CHECK(completed.at(0) == &request);
		REQUIRE_FALSE(request.error);

		auto *receive = std::get_if<pal::net::async::receive>(&request);
		REQUIRE(receive != nullptr);
		CHECK(receive->flags == 0);
		REQUIRE(receive->bytes_transferred == sent);
		CHECK(recv_view(receive->bytes_transferred) == send_view);

		REQUIRE(socket.close());
		service.run_for(run_duration, add_completed);
		CHECK(completed.at(1) == &r2);
		CHECK(r2.error == std::errc::bad_file_descriptor);
		CHECK(std::holds_alternative<pal::net::async::receive>(r2));
	}

	SECTION("async_receive: bad file descriptor") //{{{1
	{
		handle_guard{socket.native_handle()};

		socket.async_receive(&request, recv_msg);
		REQUIRE(completed.empty());

		service.run_for(run_duration, add_completed);
		CHECK(completed.at(0) == &request);
		REQUIRE(request.error == std::errc::bad_file_descriptor);
		CHECK(std::holds_alternative<pal::net::async::receive>(request));
	}

	SECTION("async_receive: cancel on socket close") //{{{1
	{
		socket.async_receive(&request, recv_msg);
		REQUIRE(completed.empty());

		REQUIRE(socket.close());

		service.run_for(run_duration, add_completed);
		CHECK(completed.at(0) == &request);
		REQUIRE(request.error == std::errc::bad_file_descriptor);
		CHECK(std::holds_alternative<pal::net::async::receive>(request));
	}

	SECTION("async_send_to / recv: single") //{{{1
	{
		socket.async_send_to(&request, send_msg[0], peer_endpoint);
		REQUIRE(completed.empty());

		auto received = pal_try(peer.receive_from(recv_msg, peer_endpoint));
		CHECK(received == send_msg[0].size_bytes());
		CHECK(peer_endpoint == endpoint);

		service.run_for(run_duration, add_completed);
		CHECK(completed.at(0) == &request);
		REQUIRE_FALSE(request.error);

		auto *send_to = std::get_if<pal::net::async::send_to>(&request);
		REQUIRE(send_to != nullptr);
		CHECK(send_to->bytes_transferred == received);
		CHECK(recv_view(send_to->bytes_transferred) == send_bufs[0]);
	}

	SECTION("async_send_to / recv: vector") //{{{1
	{
		socket.async_send_to(&request, send_msg, peer_endpoint);
		REQUIRE(completed.empty());

		auto received = peer.receive_from(recv_msg, peer_endpoint);
		CHECK(received == send_view.size());
		CHECK(peer_endpoint == endpoint);

		service.run_for(run_duration, add_completed);
		CHECK(completed.at(0) == &request);
		REQUIRE_FALSE(request.error);

		auto *send_to = std::get_if<pal::net::async::send_to>(&request);
		REQUIRE(send_to != nullptr);
		CHECK(send_to->bytes_transferred == received);
		CHECK(recv_view(send_to->bytes_transferred) == send_view);
	}

	SECTION("async_send_to: bad file descriptor") //{{{1
	{
		handle_guard{socket.native_handle()};

		socket.async_send_to(&request, send_msg, peer_endpoint);
		REQUIRE(completed.empty());

		service.run_for(run_duration, add_completed);
		CHECK(completed.at(0) == &request);
		REQUIRE(request.error == std::errc::bad_file_descriptor);
		CHECK(std::holds_alternative<pal::net::async::send_to>(request));
	}

	SECTION("async_send_to: argument list too long") //{{{1
	{
		socket.async_send_to(&request, send_msg_list_too_long, peer_endpoint);
		REQUIRE(completed.empty());

		service.run_for(run_duration, add_completed);
		CHECK(completed.at(0) == &request);
		REQUIRE(request.error == std::errc::argument_list_too_long);
		CHECK(std::holds_alternative<pal::net::async::send_to>(request));
	}

	SECTION("async_send / recv: single") //{{{1
	{
		socket.connect(peer_endpoint);
		socket.async_send(&request, send_msg[0]);
		REQUIRE(completed.empty());

		auto received = pal_try(peer.receive_from(recv_msg, peer_endpoint));
		CHECK(received == send_msg[0].size_bytes());
		CHECK(peer_endpoint == endpoint);

		service.run_for(run_duration, add_completed);
		CHECK(completed.at(0) == &request);
		REQUIRE_FALSE(request.error);

		auto *send = std::get_if<pal::net::async::send>(&request);
		REQUIRE(send != nullptr);
		CHECK(send->bytes_transferred == received);
		CHECK(recv_view(send->bytes_transferred) == send_bufs[0]);
	}

	SECTION("async_send / recv: vector") //{{{1
	{
		socket.connect(peer_endpoint);
		socket.async_send(&request, send_msg);
		REQUIRE(completed.empty());

		auto received = peer.receive_from(recv_msg, peer_endpoint);
		CHECK(received == send_view.size());
		CHECK(peer_endpoint == endpoint);

		service.run_for(run_duration, add_completed);
		CHECK(completed.at(0) == &request);
		REQUIRE_FALSE(request.error);

		auto *send = std::get_if<pal::net::async::send>(&request);
		REQUIRE(send != nullptr);
		CHECK(send->bytes_transferred == received);
		CHECK(recv_view(send->bytes_transferred) == send_view);
	}

	SECTION("async_send: bad file descriptor") //{{{1
	{
		socket.connect(peer_endpoint);

		handle_guard{socket.native_handle()};

		socket.async_send(&request, send_msg);
		REQUIRE(completed.empty());

		service.run_for(run_duration, add_completed);
		CHECK(completed.at(0) == &request);
		REQUIRE(request.error == std::errc::bad_file_descriptor);
		CHECK(std::holds_alternative<pal::net::async::send>(request));
	}

	SECTION("async_send: argument list too long") //{{{1
	{
		socket.connect(peer_endpoint);
		socket.async_send(&request, send_msg_list_too_long);
		REQUIRE(completed.empty());

		service.run_for(run_duration, add_completed);
		CHECK(completed.at(0) == &request);
		REQUIRE(request.error == std::errc::argument_list_too_long);
		CHECK(std::holds_alternative<pal::net::async::send>(request));
	}

	SECTION("async_send: not connected") //{{{1
	{
		socket.async_send(&request, send_msg);
		REQUIRE(completed.empty());

		service.run_for(run_duration, add_completed);
		CHECK(completed.at(0) == &request);
		REQUIRE(request.error == std::errc::not_connected);
		CHECK(std::holds_alternative<pal::net::async::send>(request));
	}

	//}}}1
}


} // namespace
