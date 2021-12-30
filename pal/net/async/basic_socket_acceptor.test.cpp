#include <pal/net/ip/tcp>
#include <pal/net/async/service>
#include <pal/net/test>
#include <catch2/catch_template_test_macros.hpp>
#include <deque>


namespace {


using namespace pal_test;
using namespace std::chrono_literals;


TEMPLATE_TEST_CASE("net/async/basic_socket_acceptor", "[!nonportable]",
	tcp_v4, tcp_v6, tcp_v6_only)
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

	auto acceptor = pal_try(TestType::make_acceptor());
	REQUIRE_FALSE(acceptor.has_async());

	endpoint_t accept_endpoint{TestType::loopback_v, 0};
	REQUIRE(pal_test::bind_next_available_port(acceptor, accept_endpoint));

	constexpr auto run_duration = 1s;

	SECTION("make_async: bad file descriptor") //{{{1
	{
		handle_guard{acceptor.native_handle()};
		auto make_async = service.make_async(acceptor);
		REQUIRE_FALSE(make_async);
		CHECK(make_async.error() == std::errc::bad_file_descriptor);
	}

	SECTION("async_accept / connect") //{{{1
	{
		pal_try(service.make_async(acceptor));
		pal_try(acceptor.listen());

		acceptor.async_accept(&request);
		REQUIRE(completed.empty());

		auto peer = pal_try(TestType::make_socket());
		pal_try(peer.connect(accept_endpoint));

		service.run_for(run_duration, add_completed);
		CHECK(completed.at(0) == &request);
		REQUIRE_FALSE(request.error);

		auto *accept = std::get_if<pal::net::async::accept>(&request);
		REQUIRE(accept != nullptr);
		CHECK(accept->native_handle() != pal::net::socket_base::invalid_native_handle);
	}

	SECTION("connect / async_accept") //{{{1
	{
		pal_try(service.make_async(acceptor));
		pal_try(acceptor.listen());

		auto peer = pal_try(TestType::make_socket());
		pal_try(peer.connect(accept_endpoint));

		acceptor.async_accept(&request);
		REQUIRE(completed.empty());

		service.run_for(run_duration, add_completed);
		CHECK(completed.at(0) == &request);
		REQUIRE_FALSE(request.error);

		auto *accept = std::get_if<pal::net::async::accept>(&request);
		REQUIRE(accept != nullptr);
		CHECK(accept->native_handle() != pal::net::socket_base::invalid_native_handle);
	}

	SECTION("async_accept / connect: with endpoint") //{{{1
	{
		pal_try(service.make_async(acceptor));
		pal_try(acceptor.listen());

		endpoint_t endpoint{};
		acceptor.async_accept(&request, endpoint);
		REQUIRE(completed.empty());

		auto peer = pal_try(TestType::make_socket());
		pal_try(peer.connect(accept_endpoint));

		service.run_for(run_duration, add_completed);
		CHECK(completed.at(0) == &request);
		REQUIRE_FALSE(request.error);

		auto *accept = std::get_if<pal::net::async::accept>(&request);
		REQUIRE(accept != nullptr);
		CHECK(accept->native_handle() != pal::net::socket_base::invalid_native_handle);
		CHECK(pal_try(peer.local_endpoint()) == endpoint);
	}

	SECTION("connect / async_accept: with endpoint") //{{{1
	{
		pal_try(service.make_async(acceptor));
		pal_try(acceptor.listen());

		auto peer = pal_try(TestType::make_socket());
		pal_try(peer.connect(accept_endpoint));

		endpoint_t endpoint{};
		acceptor.async_accept(&request, endpoint);
		REQUIRE(completed.empty());

		service.run_for(run_duration, add_completed);
		CHECK(completed.at(0) == &request);
		REQUIRE_FALSE(request.error);

		auto *accept = std::get_if<pal::net::async::accept>(&request);
		REQUIRE(accept != nullptr);
		CHECK(accept->native_handle() != pal::net::socket_base::invalid_native_handle);
		CHECK(pal_try(peer.local_endpoint()) == endpoint);
	}

	SECTION("async_accept / connect: one request, two connects") //{{{1
	{
		pal_try(service.make_async(acceptor));
		pal_try(acceptor.listen());

		acceptor.async_accept(&request);
		REQUIRE(completed.empty());

		auto s1 = pal_try(TestType::make_socket());
		pal_try(s1.connect(accept_endpoint));
		auto s2 = pal_try(TestType::make_socket());
		pal_try(s2.connect(accept_endpoint));

		service.run_for(run_duration, add_completed);
		CHECK(completed.at(0) == &request);
		REQUIRE_FALSE(request.error);

		auto *accept = std::get_if<pal::net::async::accept>(&request);
		REQUIRE(accept != nullptr);
		CHECK(accept->native_handle() != pal::net::socket_base::invalid_native_handle);

		// do not leave s2 hanging
		(void)acceptor.accept();
	}

	SECTION("connect / async_accept: one request, two connects") //{{{1
	{
		pal_try(service.make_async(acceptor));
		pal_try(acceptor.listen());

		auto s1 = pal_try(TestType::make_socket());
		pal_try(s1.connect(accept_endpoint));
		auto s2 = pal_try(TestType::make_socket());
		pal_try(s2.connect(accept_endpoint));

		acceptor.async_accept(&request);
		REQUIRE(completed.empty());

		service.run_for(run_duration, add_completed);
		CHECK(completed.at(0) == &request);
		REQUIRE_FALSE(request.error);

		auto *accept = std::get_if<pal::net::async::accept>(&request);
		REQUIRE(accept != nullptr);
		CHECK(accept->native_handle() != pal::net::socket_base::invalid_native_handle);

		// do not leave s2 hanging
		(void)acceptor.accept();
	}

	SECTION("async_accept / connect: two requests, one connect") //{{{1
	{
		pal_try(service.make_async(acceptor));
		pal_try(acceptor.listen());

		pal::net::async::request r1{}, r2{}, *ok = &r1, *fail = &r2;
		acceptor.async_accept(&r1);
		acceptor.async_accept(&r2);
		REQUIRE(completed.empty());

		auto peer = pal_try(TestType::make_socket());
		pal_try(peer.connect(accept_endpoint));

		service.run_for(run_duration, add_completed);
		REQUIRE(completed.size() == 1);
		if constexpr (pal::is_windows_build)
		{
			// Windows reorders pending requests, i.e. r2 is completed before r1
			// If not the case always, then do more complicated assignment here
			ok = &r2;
			fail = &r1;
		}

		CHECK(completed.at(0) == ok);
		REQUIRE_FALSE(ok->error);

		auto *accept = std::get_if<pal::net::async::accept>(ok);
		REQUIRE(accept != nullptr);
		CHECK(accept->native_handle() != pal::net::socket_base::invalid_native_handle);

		acceptor.close();

		service.run_once(add_completed);
		REQUIRE(completed.size() == 2);
		CHECK(completed.at(1) == fail);
		CHECK(fail->error == std::errc::bad_file_descriptor);
		CHECK(std::holds_alternative<pal::net::async::accept>(*fail));
	}

	SECTION("connect / async_accept: two requests, one connect") //{{{1
	{
		pal_try(service.make_async(acceptor));
		pal_try(acceptor.listen());

		auto peer = pal_try(TestType::make_socket());
		pal_try(peer.connect(accept_endpoint));

		pal::net::async::request r1{}, r2{};
		acceptor.async_accept(&r1);
		acceptor.async_accept(&r2);
		REQUIRE(completed.empty());

		service.run_for(run_duration, add_completed);
		REQUIRE(completed.size() == 1);
		CHECK(completed.at(0) == &r1);
		REQUIRE_FALSE(r1.error);

		auto *accept = std::get_if<pal::net::async::accept>(&r1);
		REQUIRE(accept != nullptr);
		CHECK(accept->native_handle() != pal::net::socket_base::invalid_native_handle);

		acceptor.close();

		service.run_once(add_completed);
		REQUIRE(completed.size() == 2);
		CHECK(completed.at(1) == &r2);
		CHECK(r2.error == std::errc::bad_file_descriptor);
		CHECK(std::holds_alternative<pal::net::async::accept>(r2));
	}

	SECTION("async_accept: not listening") //{{{1
	{
		pal_try(service.make_async(acceptor));

		acceptor.async_accept(&request);
		REQUIRE(completed.empty());

		service.run_for(run_duration, add_completed);
		CHECK(completed.at(0) == &request);
		CHECK(request.error == std::errc::invalid_argument);
		CHECK(std::holds_alternative<pal::net::async::accept>(request));
	}

	SECTION("async_accept: cancel on socket close") //{{{1
	{
		pal_try(service.make_async(acceptor));
		pal_try(acceptor.listen());

		acceptor.async_accept(&request);
		REQUIRE(completed.empty());

		pal_try(acceptor.close());

		service.run_for(run_duration, add_completed);
		CHECK(completed.at(0) == &request);
		CHECK(request.error == std::errc::bad_file_descriptor);
		CHECK(std::holds_alternative<pal::net::async::accept>(request));
	}

	SECTION("async_accept: bad file descriptor") //{{{1
	{
		pal_try(service.make_async(acceptor));
		pal_try(acceptor.listen());

		auto peer = pal_try(TestType::make_socket());
		pal_try(peer.connect(accept_endpoint));
		handle_guard{acceptor.native_handle()};
		peer.close();

		acceptor.async_accept(&request);
		REQUIRE(completed.empty());

		service.run_for(run_duration, add_completed);
		CHECK(completed.at(0) == &request);
		CHECK(request.error == std::errc::bad_file_descriptor);
		CHECK(std::holds_alternative<pal::net::async::accept>(request));
	}

	//}}}1
}


} // namespace
