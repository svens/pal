#include <pal/net/ip/tcp>
#include <pal/net/async/service>
#include <pal/net/test>
#include <deque>


namespace {


using namespace pal_test;
using namespace std::chrono_literals;


TEMPLATE_TEST_CASE("net/async/basic_stream_socket", "[!nonportable]",
	tcp_v4, tcp_v6, tcp_v6_only)
{
	using protocol_t = std::remove_cvref_t<decltype(TestType::protocol_v)>;
	using endpoint_t = typename protocol_t::endpoint;

	pal::net::async::request request{};
	std::deque<pal::net::async::request *> completed{};
	auto service = pal_try(pal::net::async::make_service(
		[&completed](auto *request)
		{
			completed.push_back(request);
		}
	));

	auto socket = pal_try(TestType::make_socket());
	REQUIRE_FALSE(socket.has_async());
	CHECK(service.make_async(socket));
	CHECK(socket.has_async());

	constexpr size_t small_size = send_view.size() / 2;
	constexpr auto run_duration = 1s;

	endpoint_t endpoint{TestType::loopback_v, 0};
	auto make_connection = [&endpoint](auto &socket)
	{
		auto acceptor = pal_try(TestType::make_acceptor());
		REQUIRE(pal_test::bind_next_available_port(acceptor, endpoint));
		pal_try(acceptor.listen());
		pal_try(socket.connect(endpoint));
		return pal_try(acceptor.accept());
	};

	SECTION("make_async: multiple times") //{{{1
	{
		if constexpr (!pal::assert_noexcept)
		{
			REQUIRE_THROWS_AS(service.make_async(socket), std::logic_error);
		}
	}

	SECTION("make_async: bad file descriptor") //{{{1
	{
		if constexpr (!pal::is_macos_build)
		{
			auto s = pal_try(TestType::make_socket());
			handle_guard{s.native_handle()};
			auto make_async = service.make_async(s);
			REQUIRE_FALSE(make_async);
			CHECK(make_async.error() == std::errc::bad_file_descriptor);
		}
		// else: registering socket with async service can't fail
	}

	SECTION("send / async_receive: single") //{{{1
	{
		auto peer = make_connection(socket);
		auto sent = pal_try(peer.send(send_msg[0]));
		CHECK(sent == send_msg[0].size_bytes());

		socket.async_receive(&request, recv_msg);

		service.run_for(run_duration);
		REQUIRE(completed.at(0) == &request);
		REQUIRE_FALSE(request.error);

		auto *receive = std::get_if<pal::net::async::receive>(&request);
		REQUIRE(receive != nullptr);
		CHECK(receive->flags == 0);
		REQUIRE(receive->bytes_transferred == sent);
		CHECK(recv_view(receive->bytes_transferred) == send_bufs[0]);
	}

	SECTION("async_receive / send: single") //{{{1
	{
		auto peer = make_connection(socket);

		socket.async_receive(&request, recv_msg);
		REQUIRE(completed.empty());

		auto sent = pal_try(peer.send(send_msg[0]));
		CHECK(sent == send_msg[0].size_bytes());

		service.run_for(run_duration);
		REQUIRE(completed.at(0) == &request);
		REQUIRE_FALSE(request.error);

		auto *receive = std::get_if<pal::net::async::receive>(&request);
		REQUIRE(receive != nullptr);
		CHECK(receive->flags == 0);
		REQUIRE(receive->bytes_transferred == send_msg[0].size_bytes());
		CHECK(recv_view(receive->bytes_transferred) == send_bufs[0]);
	}

	SECTION("send / async_receive: vector") //{{{1
	{
		auto peer = make_connection(socket);
		auto sent = pal_try(peer.send(send_msg));
		CHECK(sent == send_view.size());

		socket.async_receive(&request, recv_msg);
		REQUIRE(completed.empty());

		service.run_for(run_duration);
		REQUIRE(completed.at(0) == &request);
		REQUIRE_FALSE(request.error);

		auto *receive = std::get_if<pal::net::async::receive>(&request);
		REQUIRE(receive != nullptr);
		CHECK(receive->flags == 0);
		REQUIRE(receive->bytes_transferred == sent);
		CHECK(recv_view(receive->bytes_transferred) == send_view);
	}

	SECTION("async_receive / send: vector") //{{{1
	{
		auto peer = make_connection(socket);

		socket.async_receive(&request, recv_msg);
		REQUIRE(completed.empty());

		auto sent = pal_try(peer.send(send_msg));
		CHECK(sent == send_view.size());

		service.run_for(run_duration);
		REQUIRE(completed.at(0) == &request);
		REQUIRE_FALSE(request.error);

		auto *receive = std::get_if<pal::net::async::receive>(&request);
		REQUIRE(receive != nullptr);
		CHECK(receive->flags == 0);
		REQUIRE(receive->bytes_transferred == sent);
		CHECK(recv_view(receive->bytes_transferred) == send_view);
	}

	SECTION("send / async_receive: buffer too small") //{{{1
	{
		auto peer = make_connection(socket);
		auto sent = pal_try(peer.send(std::span{send_view}));
		CHECK(sent == send_view.size());

		socket.async_receive(&request, std::span{recv_buf, small_size});

		service.run_for(run_duration);
		REQUIRE(completed.at(0) == &request);
		REQUIRE_FALSE(request.error);

		auto *receive = std::get_if<pal::net::async::receive>(&request);
		REQUIRE(receive != nullptr);
		CHECK(receive->flags == 0);
		REQUIRE(receive->bytes_transferred == small_size);
		CHECK(recv_view(receive->bytes_transferred) == send_view.substr(0, small_size));
	}

	SECTION("async_receive / send: buffer too small") //{{{1
	{
		auto peer = make_connection(socket);

		socket.async_receive(&request, std::span{recv_buf, small_size});
		REQUIRE(completed.empty());

		auto sent = pal_try(peer.send(std::span{send_view}));
		CHECK(sent == send_view.size());

		service.run_for(run_duration);
		REQUIRE(completed.at(0) == &request);
		REQUIRE_FALSE(request.error);

		auto *receive = std::get_if<pal::net::async::receive>(&request);
		REQUIRE(receive != nullptr);
		CHECK(receive->flags == 0);
		REQUIRE(receive->bytes_transferred == small_size);
		CHECK(recv_view(receive->bytes_transferred) == send_view.substr(0, small_size));
	}

	SECTION("async_receive: argument list too long") //{{{1
	{
		auto peer = make_connection(socket);

		socket.async_receive(&request, recv_msg_list_too_long);
		REQUIRE(completed.empty());

		service.run_for(run_duration);
		REQUIRE(completed.at(0) == &request);
		REQUIRE(request.error == std::errc::argument_list_too_long);
		CHECK(std::holds_alternative<pal::net::async::receive>(request));
	}

	SECTION("async_receive: two requests, one send") //{{{1
	{
		auto peer = make_connection(socket);

		char b2[1024];
		pal::net::async::request r2;
		socket.async_receive(&request, recv_msg);
		socket.async_receive(&r2, std::span{b2});

		auto sent = pal_try(peer.send(std::span{send_view}));
		CHECK(sent == send_view.size());

		service.run_for(run_duration);
		REQUIRE(completed.size() == 1);
		REQUIRE(completed[0] == &request);
		REQUIRE_FALSE(request.error);

		auto *receive = std::get_if<pal::net::async::receive>(&request);
		REQUIRE(receive != nullptr);
		CHECK(receive->flags == 0);
		REQUIRE(receive->bytes_transferred == send_view.size());
		CHECK(recv_view(receive->bytes_transferred) == send_view);

		pal_try(socket.close());
		service.run_for(run_duration);
		REQUIRE(completed.size() == 2);
		REQUIRE(completed[1] == &r2);
		CHECK(r2.error == std::errc::bad_file_descriptor);
		CHECK(std::holds_alternative<pal::net::async::receive>(r2));
	}

	SECTION("async_receive: bad file descriptor") //{{{1
	{
		handle_guard{socket.native_handle()};

		socket.async_receive(&request, recv_msg);
		REQUIRE(completed.empty());

		service.run_for(run_duration);
		REQUIRE(completed.at(0) == &request);
		REQUIRE(request.error == std::errc::bad_file_descriptor);
		CHECK(std::holds_alternative<pal::net::async::receive>(request));
	}

	SECTION("async_receive: cancel on socket close") //{{{1
	{
		auto peer = make_connection(socket);

		socket.async_receive(&request, recv_msg);
		REQUIRE(completed.empty());

		pal_try(socket.close());

		service.run_for(run_duration);
		REQUIRE(completed.at(0) == &request);
		REQUIRE(request.error == std::errc::bad_file_descriptor);
		CHECK(std::holds_alternative<pal::net::async::receive>(request));
	}

	SECTION("async_receive: not connected") //{{{1
	{
		socket.async_receive(&request, recv_msg);
		REQUIRE(completed.empty());

		service.run_for(run_duration);
		REQUIRE(completed.at(0) == &request);
		REQUIRE(request.error == std::errc::not_connected);
		CHECK(std::holds_alternative<pal::net::async::receive>(request));
	}

	SECTION("async_receive: peer send and disconnect") //{{{1
	{
		auto peer = make_connection(socket);

		char b2[1024];
		pal::net::async::request r2;
		socket.async_receive(&request, recv_msg);
		socket.async_receive(&r2, std::span{b2});

		auto sent = pal_try(peer.send(std::span{send_view}));
		CHECK(sent == send_view.size());

		pal_try(peer.close());

		service.run_for(run_duration);
		REQUIRE(completed.size() == 2);
		REQUIRE(completed[0] == &request);
		REQUIRE(completed[1] == &r2);

		auto *receive = std::get_if<pal::net::async::receive>(&request);
		REQUIRE(receive != nullptr);
		REQUIRE(receive->bytes_transferred == sent);
		CHECK(recv_view(receive->bytes_transferred) == send_view);

		receive = std::get_if<pal::net::async::receive>(&r2);
		REQUIRE(receive != nullptr);
		CHECK(receive->bytes_transferred == 0);
	}

	SECTION("async_receive: peer disconnect") //{{{1
	{
		auto peer = make_connection(socket);
		pal_try(peer.close());

		socket.async_receive(&request, recv_msg);

		service.run_for(run_duration);
		REQUIRE(completed.at(0) == &request);

		auto *receive = std::get_if<pal::net::async::receive>(&request);
		REQUIRE(receive != nullptr);
		CHECK(receive->bytes_transferred == 0);
	}

	SECTION("async_send / recv: single") //{{{1
	{
		auto peer = make_connection(socket);
		socket.async_send(&request, send_msg[0]);

		auto received = peer.receive(recv_msg);
		REQUIRE(received == send_msg[0].size_bytes());

		service.run_for(run_duration);
		REQUIRE(completed.at(0) == &request);
		REQUIRE_FALSE(request.error);

		auto *send = std::get_if<pal::net::async::send>(&request);
		REQUIRE(send != nullptr);
		REQUIRE(send->bytes_transferred == received);
		CHECK(recv_view(send->bytes_transferred) == send_bufs[0]);
	}

	SECTION("async_send / recv: vector") //{{{1
	{
		auto peer = make_connection(socket);
		socket.async_send(&request, send_msg);

		auto received = peer.receive(recv_msg);
		REQUIRE(received == send_view.size());

		service.run_for(run_duration);
		REQUIRE(completed.at(0) == &request);
		REQUIRE_FALSE(request.error);

		auto *send = std::get_if<pal::net::async::send>(&request);
		REQUIRE(send != nullptr);
		REQUIRE(send->bytes_transferred == received);
		CHECK(recv_view(send->bytes_transferred) == send_view);
	}

	SECTION("async_send: overflow") //{{{1
	{
		if constexpr (pal::is_linux_build)
		{
			// minimal buffer on sender side
			pal::net::send_buffer_size send_buffer_size{1};
			pal_try(socket.set_option(send_buffer_size));
			pal_try(socket.get_option(send_buffer_size));

			// minimal buffer on receive side
			auto peer = make_connection(socket);
			pal::net::receive_buffer_size receive_buffer_size{1};
			pal_try(peer.set_option(receive_buffer_size));
			pal_try(peer.get_option(receive_buffer_size));

			size_t size = receive_buffer_size.value() + send_buffer_size.value();
			auto buf = std::make_unique<char[]>(size);
			REQUIRE(buf);
			std::span<char> msg{buf.get(), size};

			// overflow both sides (1+1) and also enqueue 2 requests
			std::array<pal::net::async::request, 4> requests;
			for (auto &r: requests)
			{
				socket.async_send(&r, msg);
			}

			// trigger poller that should not progress due filled buffers
			// on both side
			service.run_once();
			CHECK(completed.size() == 2);

			// drain receiver side
			peer.receive(msg);

			// let sender push 1 enqueued request, 1 remains pending
			service.run_for(500ms);
			CHECK(completed.size() == 3);

			// drain receiver again
			peer.receive(msg);

			// push last pending request
			service.run_for(500ms);

			// make sure all requests are completed
			REQUIRE(completed.size() == requests.size());
			for (auto i = 0u;  i < completed.size();  ++i)
			{
				REQUIRE(completed[i] == &requests[i]);
				REQUIRE_FALSE(requests[i].error);
				auto *send = std::get_if<pal::net::async::send>(&requests[i]);
				REQUIRE(send != nullptr);
				CHECK(send->bytes_transferred == size);
			}
		}
		// else: MacOS/Windows don't care about buffer size settings,
		// making this test meaningless
	}

	SECTION("async_send: bad file descriptor") //{{{1
	{
		handle_guard{socket.native_handle()};
		socket.async_send(&request, send_msg);

		service.run_for(run_duration);
		REQUIRE(completed.at(0) == &request);
		REQUIRE(request.error == std::errc::bad_file_descriptor);
		CHECK(std::holds_alternative<pal::net::async::send>(request));
	}

	SECTION("async_send: argument list too long") //{{{1
	{
		auto peer = make_connection(socket);

		socket.async_send(&request, send_msg_list_too_long);
		REQUIRE(completed.empty());

		service.run_for(run_duration);
		REQUIRE(completed.at(0) == &request);
		REQUIRE(request.error == std::errc::argument_list_too_long);
		CHECK(std::holds_alternative<pal::net::async::send>(request));
	}

	SECTION("async_send: not connected") //{{{1
	{
		socket.async_send(&request, send_msg);
		REQUIRE(completed.empty());

		service.run_for(run_duration);
		REQUIRE(completed.at(0) == &request);
		REQUIRE(request.error == std::errc::not_connected);
		CHECK(std::holds_alternative<pal::net::async::send>(request));
	}

	//}}}1
}


} // namespace
