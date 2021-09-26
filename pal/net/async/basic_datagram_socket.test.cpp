#include <pal/net/ip/udp>
#include <pal/net/async/service>
#include <pal/net/test>


namespace {


using namespace pal_test;
using namespace std::chrono_literals;


TEMPLATE_TEST_CASE("net/async/basic_datagram_socket", "", udp_v4, udp_v6, udp_v6_only)
{
	using protocol_t = std::remove_cvref_t<decltype(TestType::protocol_v)>;
	using endpoint_t = typename protocol_t::endpoint;

	pal::net::async::request request{}, *request_ptr = nullptr;
	auto make_service = pal::net::async::make_service(
		[&request_ptr](auto *request)
		{
			request_ptr = request;
		}
	);
	REQUIRE(make_service);
	auto service = std::move(*make_service);

	auto socket = TestType::make_socket().value();
	endpoint_t endpoint{TestType::loopback_v, 0};
	REQUIRE(pal_test::bind_next_available_port(socket, endpoint));

	auto helper = TestType::make_socket().value();
	REQUIRE(helper.bind({TestType::loopback_v, 0}));
	auto helper_endpoint = helper.local_endpoint().value();

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

	SECTION("send / async_receive_from: single") //{{{1
	{
		auto send = helper.send_to(send_msg[0], endpoint);
		REQUIRE(send);
		CHECK(*send == send_msg[0].size_bytes());

		socket.async_receive_from(&request, recv_msg, endpoint);

		service.run_for(run_duration);
		REQUIRE(request_ptr == &request);
		REQUIRE_FALSE(request.error);

		auto *receive_from = std::get_if<pal::net::async::receive_from>(&request);
		REQUIRE(receive_from != nullptr);
		CHECK(receive_from->flags == 0);
		REQUIRE(receive_from->bytes_transferred == send_msg[0].size_bytes());
		CHECK(recv_view(receive_from->bytes_transferred) == send_bufs[0]);

		CHECK(endpoint == helper_endpoint);
	}

	SECTION("async_receive_from / send: single") //{{{1
	{
		socket.async_receive_from(&request, recv_msg, endpoint);
		REQUIRE(request_ptr == nullptr);

		auto send = helper.send_to(send_msg[0], endpoint);
		REQUIRE(send);
		CHECK(*send == send_msg[0].size_bytes());

		service.run_for(run_duration);
		REQUIRE(request_ptr == &request);
		REQUIRE_FALSE(request.error);

		auto *receive_from = std::get_if<pal::net::async::receive_from>(&request);
		REQUIRE(receive_from != nullptr);
		CHECK(receive_from->flags == 0);
		REQUIRE(receive_from->bytes_transferred == send_msg[0].size_bytes());
		CHECK(recv_view(receive_from->bytes_transferred) == send_bufs[0]);

		CHECK(endpoint == helper_endpoint);
	}

	SECTION("send / async_receive_from: vector") //{{{1
	{
		auto send = helper.send_to(send_msg, endpoint);
		REQUIRE(send);
		CHECK(*send == send_view.size());

		socket.async_receive_from(&request, recv_msg, endpoint);

		service.run_for(run_duration);
		REQUIRE(request_ptr == &request);
		REQUIRE_FALSE(request.error);

		auto *receive_from = std::get_if<pal::net::async::receive_from>(&request);
		REQUIRE(receive_from != nullptr);
		CHECK(receive_from->flags == 0);
		REQUIRE(receive_from->bytes_transferred == send_view.size());
		CHECK(recv_view(receive_from->bytes_transferred) == send_view);

		CHECK(endpoint == helper_endpoint);
	}

	SECTION("async_receive_from / send: vector") //{{{1
	{
		socket.async_receive_from(&request, recv_msg, endpoint);
		REQUIRE(request_ptr == nullptr);

		auto send = helper.send_to(send_msg, endpoint);
		REQUIRE(send);
		CHECK(*send == send_view.size());

		service.run_for(run_duration);
		REQUIRE(request_ptr == &request);
		REQUIRE_FALSE(request.error);

		auto *receive_from = std::get_if<pal::net::async::receive_from>(&request);
		REQUIRE(receive_from != nullptr);
		CHECK(receive_from->flags == 0);
		REQUIRE(receive_from->bytes_transferred == send_view.size());
		CHECK(recv_view(receive_from->bytes_transferred) == send_view);

		CHECK(endpoint == helper_endpoint);
	}

	SECTION("send / async_receive_from: buffer too small") //{{{1
	{
		auto send = helper.send_to(std::span{send_view}, endpoint);
		REQUIRE(send);
		CHECK(*send == send_view.size());

		socket.async_receive_from(&request, std::span{recv_buf, small_size}, endpoint);

		service.run_for(run_duration);
		REQUIRE(request_ptr == &request);
		REQUIRE_FALSE(request.error);

		auto *receive_from = std::get_if<pal::net::async::receive_from>(&request);
		REQUIRE(receive_from != nullptr);
		CHECK(receive_from->flags == socket.message_truncated);
		REQUIRE(receive_from->bytes_transferred == small_size);
		CHECK(recv_view(receive_from->bytes_transferred) == send_view.substr(0, small_size));

		CHECK(endpoint == helper_endpoint);
	}

	SECTION("async_receive_from / send: buffer too small") //{{{1
	{
		socket.async_receive_from(&request, std::span{recv_buf, small_size}, endpoint);
		REQUIRE(request_ptr == nullptr);

		auto send = helper.send_to(std::span{send_view}, endpoint);
		REQUIRE(send);
		CHECK(*send == send_view.size());

		service.run_for(run_duration);
		REQUIRE(request_ptr == &request);
		REQUIRE_FALSE(request.error);

		auto *receive_from = std::get_if<pal::net::async::receive_from>(&request);
		REQUIRE(receive_from != nullptr);
		CHECK(receive_from->flags == socket.message_truncated);
		REQUIRE(receive_from->bytes_transferred == small_size);
		CHECK(recv_view(receive_from->bytes_transferred) == send_view.substr(0, small_size));

		CHECK(endpoint == helper_endpoint);
	}

	SECTION("async_receive_from: argument list too long") //{{{1
	{
		socket.async_receive_from(&request, recv_msg_list_too_long, endpoint);
		REQUIRE(request_ptr == nullptr);

		service.run_for(run_duration);
		REQUIRE(request_ptr == &request);
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

		auto send = helper.send_to(std::span{send_view}, endpoint);
		REQUIRE(send);
		CHECK(*send == send_view.size());

		service.run_for(run_duration);
		REQUIRE(request_ptr == &request);
		REQUIRE_FALSE(request.error);

		auto *receive_from = std::get_if<pal::net::async::receive_from>(&request);
		REQUIRE(receive_from != nullptr);
		CHECK(receive_from->flags == 0);
		REQUIRE(receive_from->bytes_transferred == send_view.size());
		CHECK(recv_view(receive_from->bytes_transferred) == send_view);

		REQUIRE(socket.close());
		service.run_for(run_duration);
		REQUIRE(request_ptr == &r2);
		CHECK(r2.error == std::errc::bad_file_descriptor);
		CHECK(std::holds_alternative<pal::net::async::receive_from>(r2));
	}

	SECTION("async_receive_from: bad file descriptor") //{{{1
	{
		handle_guard{socket.native_handle()};

		socket.async_receive_from(&request, recv_msg, endpoint);
		REQUIRE(request_ptr == nullptr);

		service.run_for(run_duration);
		REQUIRE(request_ptr == &request);
		REQUIRE(request.error == std::errc::bad_file_descriptor);
		CHECK(std::holds_alternative<pal::net::async::receive_from>(request));
	}

	SECTION("async_receive_from: cancel on socket close") //{{{1
	{
		socket.async_receive_from(&request, recv_msg, endpoint);
		REQUIRE(request_ptr == nullptr);

		REQUIRE(socket.close());

		service.run_for(run_duration);
		REQUIRE(request_ptr == &request);
		REQUIRE(request.error == std::errc::bad_file_descriptor);
		CHECK(std::holds_alternative<pal::net::async::receive_from>(request));
	}

	SECTION("send / async_receive: single") //{{{1
	{
		auto send = helper.send_to(send_msg[0], endpoint);
		REQUIRE(send);
		CHECK(*send == send_msg[0].size_bytes());

		socket.async_receive(&request, recv_msg);

		service.run_for(run_duration);
		REQUIRE(request_ptr == &request);
		REQUIRE_FALSE(request.error);

		auto *receive = std::get_if<pal::net::async::receive>(&request);
		REQUIRE(receive != nullptr);
		CHECK(receive->flags == 0);
		REQUIRE(receive->bytes_transferred == send_msg[0].size_bytes());
		CHECK(recv_view(receive->bytes_transferred) == send_bufs[0]);
	}

	SECTION("async_receive / send: single") //{{{1
	{
		socket.async_receive(&request, recv_msg);
		REQUIRE(request_ptr == nullptr);

		auto send = helper.send_to(send_msg[0], endpoint);
		REQUIRE(send);
		CHECK(*send == send_msg[0].size_bytes());

		service.run_for(run_duration);
		REQUIRE(request_ptr == &request);
		REQUIRE_FALSE(request.error);

		auto *receive = std::get_if<pal::net::async::receive>(&request);
		REQUIRE(receive != nullptr);
		CHECK(receive->flags == 0);
		REQUIRE(receive->bytes_transferred == send_msg[0].size_bytes());
		CHECK(recv_view(receive->bytes_transferred) == send_bufs[0]);
	}

	SECTION("send / async_receive: vector") //{{{1
	{
		auto send = helper.send_to(send_msg, endpoint);
		REQUIRE(send);
		CHECK(*send == send_view.size());

		socket.async_receive(&request, recv_msg);

		service.run_for(run_duration);
		REQUIRE(request_ptr == &request);
		REQUIRE_FALSE(request.error);

		auto *receive = std::get_if<pal::net::async::receive>(&request);
		REQUIRE(receive != nullptr);
		CHECK(receive->flags == 0);
		REQUIRE(receive->bytes_transferred == send_view.size());
		CHECK(recv_view(receive->bytes_transferred) == send_view);
	}

	SECTION("async_receive / send: vector") //{{{1
	{
		socket.async_receive(&request, recv_msg);
		REQUIRE(request_ptr == nullptr);

		auto send = helper.send_to(send_msg, endpoint);
		REQUIRE(send);
		CHECK(*send == send_view.size());

		service.run_for(run_duration);
		REQUIRE(request_ptr == &request);
		REQUIRE_FALSE(request.error);

		auto *receive = std::get_if<pal::net::async::receive>(&request);
		REQUIRE(receive != nullptr);
		CHECK(receive->flags == 0);
		REQUIRE(receive->bytes_transferred == send_view.size());
		CHECK(recv_view(receive->bytes_transferred) == send_view);
	}

	SECTION("send / async_receive: buffer too small") //{{{1
	{
		auto send = helper.send_to(std::span{send_view}, endpoint);
		REQUIRE(send);
		CHECK(*send == send_view.size());

		socket.async_receive(&request, std::span{recv_buf, small_size});

		service.run_for(run_duration);
		REQUIRE(request_ptr == &request);
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
		REQUIRE(request_ptr == nullptr);

		auto send = helper.send_to(std::span{send_view}, endpoint);
		REQUIRE(send);
		CHECK(*send == send_view.size());

		service.run_for(run_duration);
		REQUIRE(request_ptr == &request);
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
		REQUIRE(request_ptr == nullptr);

		service.run_for(run_duration);
		REQUIRE(request_ptr == &request);
		REQUIRE(request.error == std::errc::argument_list_too_long);
		CHECK(std::holds_alternative<pal::net::async::receive>(request));
	}

	SECTION("async_receive: two requests, one datagram") //{{{1
	{
		char b2[1024];
		pal::net::async::request r2;
		socket.async_receive(&request, recv_msg);
		socket.async_receive(&r2, std::span{b2});

		auto send = helper.send_to(std::span{send_view}, endpoint);
		REQUIRE(send);
		CHECK(*send == send_view.size());

		service.run_for(run_duration);
		REQUIRE(request_ptr == &request);
		REQUIRE_FALSE(request.error);

		auto *receive = std::get_if<pal::net::async::receive>(&request);
		REQUIRE(receive != nullptr);
		CHECK(receive->flags == 0);
		REQUIRE(receive->bytes_transferred == send_view.size());
		CHECK(recv_view(receive->bytes_transferred) == send_view);

		REQUIRE(socket.close());
		service.run_for(run_duration);
		REQUIRE(request_ptr == &r2);
		CHECK(r2.error == std::errc::bad_file_descriptor);
		CHECK(std::holds_alternative<pal::net::async::receive>(r2));
	}

	SECTION("async_receive: bad file descriptor") //{{{1
	{
		handle_guard{socket.native_handle()};

		socket.async_receive(&request, recv_msg);
		REQUIRE(request_ptr == nullptr);

		service.run_for(run_duration);
		REQUIRE(request_ptr == &request);
		REQUIRE(request.error == std::errc::bad_file_descriptor);
		CHECK(std::holds_alternative<pal::net::async::receive>(request));
	}

	SECTION("async_receive: cancel on socket close") //{{{1
	{
		socket.async_receive(&request, recv_msg);
		REQUIRE(request_ptr == nullptr);

		REQUIRE(socket.close());

		service.run_for(run_duration);
		REQUIRE(request_ptr == &request);
		REQUIRE(request.error == std::errc::bad_file_descriptor);
		CHECK(std::holds_alternative<pal::net::async::receive>(request));
	}

	SECTION("async_send_to / recv: single") //{{{1
	{
		socket.async_send_to(&request, send_msg[0], helper_endpoint);
		REQUIRE(request_ptr == nullptr);

		auto receive_from = helper.receive_from(recv_msg, helper_endpoint);
		REQUIRE(receive_from);
		CHECK(*receive_from == send_msg[0].size_bytes());
		CHECK(helper_endpoint == endpoint);

		service.run_for(run_duration);
		REQUIRE(request_ptr == &request);
		REQUIRE_FALSE(request.error);

		auto *send_to = std::get_if<pal::net::async::send_to>(&request);
		REQUIRE(send_to != nullptr);
		CHECK(send_to->bytes_transferred == send_msg[0].size_bytes());
		CHECK(recv_view(send_to->bytes_transferred) == send_bufs[0]);
	}

	SECTION("async_send_to / recv: vector") //{{{1
	{
		socket.async_send_to(&request, send_msg, helper_endpoint);
		REQUIRE(request_ptr == nullptr);

		auto receive_from = helper.receive_from(recv_msg, helper_endpoint);
		REQUIRE(receive_from);
		CHECK(*receive_from == send_view.size());
		CHECK(helper_endpoint == endpoint);

		service.run_for(run_duration);
		REQUIRE(request_ptr == &request);
		REQUIRE_FALSE(request.error);

		auto *send_to = std::get_if<pal::net::async::send_to>(&request);
		REQUIRE(send_to != nullptr);
		CHECK(send_to->bytes_transferred == send_view.size());
		CHECK(recv_view(send_to->bytes_transferred) == send_view);
	}

	SECTION("async_send_to: bad file descriptor") //{{{1
	{
		handle_guard{socket.native_handle()};

		socket.async_send_to(&request, send_msg, helper_endpoint);
		REQUIRE(request_ptr == nullptr);

		service.run_for(run_duration);
		REQUIRE(request_ptr == &request);
		REQUIRE(request.error == std::errc::bad_file_descriptor);
		CHECK(std::holds_alternative<pal::net::async::send_to>(request));
	}

	SECTION("async_send_to: argument list too long") //{{{1
	{
		socket.async_send_to(&request, send_msg_list_too_long, helper_endpoint);
		REQUIRE(request_ptr == nullptr);

		service.run_for(run_duration);
		REQUIRE(request_ptr == &request);
		REQUIRE(request.error == std::errc::argument_list_too_long);
		CHECK(std::holds_alternative<pal::net::async::send_to>(request));
	}

	SECTION("async_send / recv: single") //{{{1
	{
		socket.connect(helper_endpoint);
		socket.async_send(&request, send_msg[0]);
		REQUIRE(request_ptr == nullptr);

		auto receive_from = helper.receive_from(recv_msg, helper_endpoint);
		REQUIRE(receive_from);
		CHECK(*receive_from == send_msg[0].size_bytes());
		CHECK(helper_endpoint == endpoint);

		service.run_for(run_duration);
		REQUIRE(request_ptr == &request);
		REQUIRE_FALSE(request.error);

		auto *send = std::get_if<pal::net::async::send>(&request);
		REQUIRE(send != nullptr);
		CHECK(send->bytes_transferred == send_msg[0].size_bytes());
		CHECK(recv_view(send->bytes_transferred) == send_bufs[0]);
	}

	SECTION("async_send / recv: vector") //{{{1
	{
		socket.connect(helper_endpoint);
		socket.async_send(&request, send_msg);
		REQUIRE(request_ptr == nullptr);

		auto receive_from = helper.receive_from(recv_msg, helper_endpoint);
		REQUIRE(receive_from);
		CHECK(*receive_from == send_view.size());
		CHECK(helper_endpoint == endpoint);

		service.run_for(run_duration);
		REQUIRE(request_ptr == &request);
		REQUIRE_FALSE(request.error);

		auto *send = std::get_if<pal::net::async::send>(&request);
		REQUIRE(send != nullptr);
		CHECK(send->bytes_transferred == send_view.size());
		CHECK(recv_view(send->bytes_transferred) == send_view);
	}

	SECTION("async_send: bad file descriptor") //{{{1
	{
		socket.connect(helper_endpoint);

		handle_guard{socket.native_handle()};

		socket.async_send(&request, send_msg);
		REQUIRE(request_ptr == nullptr);

		service.run_for(run_duration);
		REQUIRE(request_ptr == &request);
		REQUIRE(request.error == std::errc::bad_file_descriptor);
		CHECK(std::holds_alternative<pal::net::async::send>(request));
	}

	SECTION("async_send: argument list too long") //{{{1
	{
		socket.connect(helper_endpoint);
		socket.async_send(&request, send_msg_list_too_long);
		REQUIRE(request_ptr == nullptr);

		service.run_for(run_duration);
		REQUIRE(request_ptr == &request);
		REQUIRE(request.error == std::errc::argument_list_too_long);
		CHECK(std::holds_alternative<pal::net::async::send>(request));
	}

	SECTION("async_send: not connected") //{{{1
	{
		socket.async_send(&request, send_msg);
		REQUIRE(request_ptr == nullptr);

		service.run_for(run_duration);
		REQUIRE(request_ptr == &request);
		REQUIRE(request.error == std::errc::not_connected);
		CHECK(std::holds_alternative<pal::net::async::send>(request));
	}

	//}}}1

	SECTION("no recursion") //{{{1
	{
		// just for case if some test alread grabbed helper
		REQUIRE_FALSE(helper.has_async());

		// Library/OS may complete asynchronous request immediately
		// and notify application. In this test we check that if that
		// happens and application immediately starts another request
		// in callback, this ping-ponging does not get into recursive
		// loop
		//
		// In following tests, there are two poll syscalls:
		// 1) after 1st, application layer should receive two
		//    callbacks: one for request started in main, second for
		//    request started in callback itself
		// 2) in second callback, test starts another request but we
		//    should not receive callback yet, it would mean we are in
		//    loop
		// 3) instead, in main, we invoke 2nd poll that would deliver
		//    us this callback now

		std::array<pal::net::async::request, 3> requests;
		int call_depth = 0;
		size_t call_count = 0;
		enum { receive_from, receive, send_to, send, stop } new_request = stop;

		auto svc = pal::net::async::make_service([&](auto *request)
		{
			auto i = call_count++;
			REQUIRE(i < requests.size());
			REQUIRE(request == &requests[i]);
			REQUIRE(++call_depth == 1);

			if (new_request == receive_from)
			{
				helper.async_receive_from(
					&requests[call_count],
					recv_msg,
					endpoint
				);
			}
			else if (new_request == receive)
			{
				helper.async_receive(
					&requests[call_count],
					recv_msg
				);
			}
			else if (new_request == send_to)
			{
				helper.async_send_to(
					&requests[call_count],
					send_msg[call_count],
					endpoint
				);
			}
			else if (new_request == send)
			{
				helper.async_send(
					&requests[call_count],
					send_msg[call_count]
				);
			}

			REQUIRE(--call_depth == 0);
		}).value();

		REQUIRE(svc.make_async(helper));
		REQUIRE(helper.has_async());

		SECTION("async_receive_from")
		{
			socket.send_to(send_msg[0], helper_endpoint);
			socket.send_to(send_msg[1], helper_endpoint);
			socket.send_to(send_msg[2], helper_endpoint);

			new_request = receive_from;
			helper.async_receive_from(&requests[0], recv_msg, endpoint);

			svc.run_for(run_duration);
			CHECK(call_count == 2);

			new_request = stop;
			svc.run_for(run_duration);
			CHECK(call_count == 3);
		}

		SECTION("async_receive")
		{
			socket.send_to(send_msg[0], helper_endpoint);
			socket.send_to(send_msg[1], helper_endpoint);
			socket.send_to(send_msg[2], helper_endpoint);

			new_request = receive;
			helper.async_receive(&requests[0], recv_msg);

			svc.run_for(run_duration);
			CHECK(call_count == 2);

			new_request = stop;
			svc.run_for(run_duration);
			CHECK(call_count == 3);
		}

		SECTION("async_send_to")
		{
			new_request = send_to;
			helper.async_send_to(&requests[0], send_msg[0], endpoint);

			svc.run_for(run_duration);
			CHECK(call_count == 2);

			new_request = stop;
			svc.run_for(run_duration);
			CHECK(call_count == 3);
		}

		SECTION("async_send")
		{
			new_request = send;
			helper.async_send(&requests[0], send_msg[0]);

			svc.run_for(run_duration);
			CHECK(call_count == 2);

			new_request = stop;
			svc.run_for(run_duration);
			CHECK(call_count == 3);
		}
	}

	//}}}1
}


} // namespace
