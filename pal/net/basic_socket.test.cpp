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

	typename protocol_t::socket s;
	CHECK_FALSE(s.is_open());

	auto r = TestType::make_socket();
	REQUIRE(r);
	s = std::move(*r);
	REQUIRE(s.is_open());
	CHECK(s.native_handle() != -1);
	CHECK(s.protocol() == TestType::protocol_v);

	SECTION("move ctor")
	{
		auto s1 = std::move(s);
		CHECK(s1.is_open());
		CHECK_FALSE(s.is_open());
	}

	SECTION("move assign")
	{
		std::decay_t<decltype(s)> s1;
		s1 = std::move(s);
		CHECK(s1.is_open());
		CHECK(s1.protocol() == TestType::protocol_v);
		CHECK_FALSE(s.is_open());
	}

	SECTION("release")
	{
		pal_test::handle_guard guard{s.release()};
		CHECK_FALSE(s.is_open());
	}

	SECTION("open")
	{
		pal_test::handle_guard guard{s.release()};
		REQUIRE_FALSE(s.is_open());
		REQUIRE(s.open(TestType::protocol_v));
		CHECK(s.is_open());
		CHECK(s.protocol() == TestType::protocol_v);
	}

	SECTION("open already opened")
	{
		if constexpr (!pal::assert_noexcept)
		{
			REQUIRE_THROWS_AS(s.open(TestType::protocol_v), std::logic_error);
		}
		else
		{
			// although will succeed in release build, it is still runtime
			// penalty for compile-time logic error
			auto handle = s.native_handle();
			REQUIRE(s.open(TestType::protocol_v));
			CHECK(handle != s.native_handle());
		}
	}

	SECTION("assign opened")
	{
		auto new_handle = TestType::make_socket().value().release();
		auto old_handle = s.native_handle();
		REQUIRE(s.assign(TestType::protocol_v, new_handle));
		CHECK(s.native_handle() == new_handle);
		CHECK(s.native_handle() != old_handle);
		CHECK(s.protocol() == TestType::protocol_v);
	}

	SECTION("assign closed")
	{
		auto new_handle = TestType::make_socket().value().release();
		pal_test::handle_guard guard{s.release()};
		REQUIRE(s.assign(TestType::protocol_v, new_handle));
		CHECK(s.native_handle() == new_handle);
		CHECK(s.native_handle() != guard.handle);
		CHECK(s.protocol() == TestType::protocol_v);
	}

	SECTION("assign invalid")
	{
		REQUIRE(s.assign(TestType::protocol_v, pal::net::socket_base::invalid_native_handle));
		REQUIRE_FALSE(s.is_open());
	}

	SECTION("assign invalid to closed")
	{
		REQUIRE(s.close());
		REQUIRE(s.assign(TestType::protocol_v, pal::net::socket_base::invalid_native_handle));
		REQUIRE_FALSE(s.is_open());
	}

	SECTION("assign bad_alloc")
	{
		pal_test::handle_guard guard{s.release()};
		pal_test::bad_alloc_once x;
		auto assign = s.assign(TestType::protocol_v, guard.handle);
		REQUIRE_FALSE(assign);
		CHECK(assign.error() == std::errc::not_enough_memory);
	}

	SECTION("close")
	{
		s.close();
		CHECK_FALSE(s.is_open());
	}

	SECTION("close invalidated")
	{
		pal_test::handle_guard{s.native_handle()};
		auto close = s.close();
		REQUIRE_FALSE(close);
		CHECK(close.error() == std::errc::bad_file_descriptor);
	}

	SECTION("close already closed")
	{
		s.close();
		CHECK_FALSE(s.is_open());

		if constexpr (!pal::assert_noexcept)
		{
			REQUIRE_THROWS_AS(s.close(), std::logic_error);
		}
	}

	SECTION("make bad_alloc")
	{
		pal_test::handle_guard guard{s.release()};
		pal_test::bad_alloc_once x;
		r = TestType::make_socket();
		REQUIRE_FALSE(r);
		CHECK(r.error() == std::errc::not_enough_memory);
	}

	SECTION("local_endpoint")
	{
		// see SECTION("bind")
		SUCCEED();

		SECTION("unbound")
		{
			auto local_endpoint = s.local_endpoint();
			REQUIRE(local_endpoint);
			CHECK(local_endpoint->address().is_v4() == pal_test::is_v4_v<TestType>);
			CHECK(local_endpoint->address().is_unspecified());
			CHECK(local_endpoint->port() == 0);
		}

		SECTION("bad file descriptor")
		{
			pal_test::handle_guard{s.native_handle()};
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
			pal_test::handle_guard{s.native_handle()};
			auto remote_endpoint = s.remote_endpoint();
			REQUIRE_FALSE(remote_endpoint);
			CHECK(remote_endpoint.error() == std::errc::bad_file_descriptor);
		}
	}

	SECTION("bind")
	{
		endpoint_t endpoint{TestType::loopback_v, 0};
		REQUIRE(pal_test::bind_next_available_port(s, endpoint));
		CHECK(s.local_endpoint().value() == endpoint);

		SECTION("address in use")
		{
			r = TestType::make_socket();
			REQUIRE(r);
			auto bind = r->bind(endpoint);
			REQUIRE_FALSE(bind);
			CHECK(bind.error() == std::errc::address_in_use);
		}

		SECTION("bad file descriptor")
		{
			pal_test::handle_guard{s.native_handle()};
			auto bind = s.bind(endpoint);
			REQUIRE_FALSE(bind);
			CHECK(bind.error() == std::errc::bad_file_descriptor);
		}
	}

	SECTION("connect")
	{
		endpoint_t endpoint{TestType::loopback_v, 0};

		if constexpr (pal_test::is_tcp_v<TestType>)
		{
			auto a = TestType::make_acceptor().value();
			REQUIRE(pal_test::bind_next_available_port(a, endpoint));
			REQUIRE(a.listen());

			SECTION("success")
			{
				auto connect = s.connect(endpoint);
				REQUIRE(connect);
				CHECK(s.remote_endpoint().value() == endpoint);
			}

			SECTION("no listener")
			{
				REQUIRE(a.close());
				auto connect = s.connect(endpoint);
				REQUIRE_FALSE(connect);
				CHECK(connect.error() == std::errc::connection_refused);
			}
		}
		else if constexpr (pal_test::is_udp_v<TestType>)
		{
			endpoint.port(pal_test::next_port(TestType::protocol_v));
			auto connect = s.connect(endpoint);
			REQUIRE(connect);
			CHECK(s.remote_endpoint().value() == endpoint);
		}

		SECTION("bad file descriptor")
		{
			pal_test::handle_guard{s.native_handle()};
			auto connect = s.connect(endpoint);
			REQUIRE_FALSE(connect);
			CHECK(connect.error() == std::errc::bad_file_descriptor);
		}
	}

	SECTION("native_non_blocking")
	{
		if constexpr (pal::is_windows_build)
		{
			auto set_mode = s.native_non_blocking();
			REQUIRE_FALSE(set_mode);
			CHECK(set_mode.error() == std::errc::operation_not_supported);

			REQUIRE(s.native_non_blocking(true));
		}
		else
		{
			// default off
			CHECK_FALSE(s.native_non_blocking().value());

			// turn on, check
			REQUIRE(s.native_non_blocking(true));
			CHECK(s.native_non_blocking().value());

			// turn off, check
			REQUIRE(s.native_non_blocking(false));
			CHECK_FALSE(s.native_non_blocking().value());
		}

		SECTION("bad file descriptor")
		{
			pal_test::handle_guard{s.native_handle()};
			auto get_mode = s.native_non_blocking();
			REQUIRE_FALSE(get_mode);

			if constexpr (pal::is_windows_build)
			{
				CHECK(get_mode.error() == std::errc::operation_not_supported);
			}
			else
			{
				CHECK(get_mode.error() == std::errc::bad_file_descriptor);
			}

			auto set_mode = s.native_non_blocking(false);
			REQUIRE_FALSE(set_mode);
			CHECK(set_mode.error() == std::errc::bad_file_descriptor);
		}
	}

	SECTION("available")
	{
		auto available = s.available();
		REQUIRE(available);
		CHECK(*available == 0);

		SECTION("bad file descriptor")
		{
			pal_test::handle_guard{s.native_handle()};
			available = s.available();
			REQUIRE_FALSE(available);
			CHECK(available.error() == std::errc::bad_file_descriptor);
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
