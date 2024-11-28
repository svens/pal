#include <pal/net/basic_socket>
#include <pal/net/test>
#include <catch2/catch_template_test_macros.hpp>

namespace {

using namespace pal_test;

TEMPLATE_TEST_CASE("net/basic_socket", "[!nonportable]",
	udp_v4,
	tcp_v4,
	udp_v6,
	tcp_v6,
	udp_v6_only,
	tcp_v6_only)
{
	using protocol_t = std::remove_cvref_t<decltype(TestType::protocol_v)>;
	using endpoint_t = typename protocol_t::endpoint;

	auto s = TestType::make_socket().value();
	REQUIRE(s);
	CHECK(s.native_socket()->handle != pal::net::native_socket_handle::invalid);
	CHECK(s.protocol() == TestType::protocol_v);

	SECTION("move ctor")
	{
		auto s1 = std::move(s);
		REQUIRE(s1);
		CHECK(s1.native_socket()->handle != pal::net::native_socket_handle::invalid);
		CHECK(s1.protocol() == TestType::protocol_v);
		CHECK(!s);
	}

	SECTION("move assign")
	{
		auto s_orig_handle = s.native_socket()->handle;
		auto s1 = TestType::make_socket().value();
		s1 = std::move(s);
		CHECK(!s);
		CHECK(s1.native_socket()->handle == s_orig_handle);
	}

	SECTION("release")
	{
		auto s_orig_handle = s.native_socket()->handle;
		auto native = s.release();
		CHECK(!s);
		CHECK(native->handle == s_orig_handle);
	}

	SECTION("bind")
	{
		endpoint_t endpoint{TestType::loopback_v, 0};
		REQUIRE(bind_next_available_port(s, endpoint));
		CHECK(s.local_endpoint().value() == endpoint);

		SECTION("address in use")
		{
			auto s1 = TestType::make_socket().value();
			auto bind = s1.bind(endpoint);
			REQUIRE_FALSE(bind);
			CHECK(bind.error() == std::errc::address_in_use);
		}

		SECTION("bad file descriptor")
		{
			close_native_handle(s);
			auto bind = s.bind(endpoint);
			REQUIRE_FALSE(bind);
			CHECK(bind.error() == std::errc::bad_file_descriptor);
		}
	}

	SECTION("connect")
	{
		endpoint_t endpoint{TestType::loopback_v, 0};

		if constexpr (is_tcp_v<TestType>)
		{
			auto a = TestType::make_acceptor().value();
			REQUIRE(bind_next_available_port(a, endpoint));
			REQUIRE_NOTHROW(a.listen().value());

			SECTION("success")
			{
				REQUIRE_NOTHROW(s.connect(endpoint).value());
				CHECK(s.remote_endpoint() == endpoint);
			}

			SECTION("no listener")
			{
				close_native_handle(a);
				auto connect = s.connect(endpoint);
				REQUIRE_FALSE(connect);
				CHECK(connect.error() == std::errc::connection_refused);
			}
		}
		else if constexpr (is_udp_v<TestType>)
		{
			endpoint.port(next_port(TestType::protocol_v));
			REQUIRE_NOTHROW(s.connect(endpoint).value());
			CHECK(s.remote_endpoint().value() == endpoint);
		}

		SECTION("bad file descriptor")
		{
			close_native_handle(s);
			auto connect = s.connect(endpoint);
			REQUIRE_FALSE(connect);
			CHECK(connect.error() == std::errc::bad_file_descriptor);
		}
	}

	SECTION("shutdown")
	{
		// see:
		// - pal/net/ip/tcp.test.cpp
		// - pal/net/ip/udp.test.cpp
		SUCCEED();

		SECTION("not connected")
		{
			auto shutdown = s.shutdown(s.shutdown_both);
			if constexpr (pal::os == pal::os_type::windows && pal_test::is_udp_v<TestType>)
			{
				REQUIRE(shutdown);
			}
			else
			{
				REQUIRE_FALSE(shutdown);
				CHECK(shutdown.error() == std::errc::not_connected);
			}
		}

		SECTION("bad file descriptor")
		{
			close_native_handle(s);
			auto shutdown = s.shutdown(s.shutdown_both);
			REQUIRE_FALSE(shutdown);
			CHECK(shutdown.error() == std::errc::bad_file_descriptor);
		}
	}

	SECTION("local_endpoint")
	{
		// see SECTION("bind")
		SUCCEED();

		SECTION("unbound")
		{
			auto local_endpoint = s.local_endpoint();
			REQUIRE(local_endpoint);
			CHECK(has_expected_family<TestType>(local_endpoint->address()));
			CHECK(local_endpoint->address().is_unspecified());
			CHECK(local_endpoint->port() == 0);
		}

		SECTION("bad file descriptor")
		{
			close_native_handle(s);
			auto local_endpoint = s.local_endpoint();
			REQUIRE_FALSE(local_endpoint);
			CHECK(local_endpoint.error() == std::errc::bad_file_descriptor);
		}
	}

	SECTION("remote_endpoint")
	{
		// see SECTION("connect")
		SUCCEED();

		SECTION("not connected")
		{
			auto remote_endpoint = s.remote_endpoint();
			REQUIRE_FALSE(remote_endpoint);
			CHECK(remote_endpoint.error() == std::errc::not_connected);
		}

		SECTION("bad file descriptor")
		{
			close_native_handle(s);
			auto remote_endpoint = s.remote_endpoint();
			REQUIRE_FALSE(remote_endpoint);
			CHECK(remote_endpoint.error() == std::errc::bad_file_descriptor);
		}
	}
}

TEMPLATE_TEST_CASE("net/basic_socket", "",
	invalid_protocol)
{
	SECTION("make_datagram_socket")
	{
		auto s = pal::net::make_datagram_socket(TestType());
		REQUIRE_FALSE(s);
		CHECK(s.error() == std::errc::protocol_not_supported);
	}

	SECTION("make_stream_socket")
	{
		auto s = pal::net::make_stream_socket(TestType());
		REQUIRE_FALSE(s);
		CHECK(s.error() == std::errc::protocol_not_supported);
	}
}

} // namespace
