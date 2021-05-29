#include <pal/net/test>


namespace {


using namespace pal_test;


TEMPLATE_TEST_CASE("net/basic_socket", "",
	udp_v4,
	tcp_v4,
	udp_v6,
	tcp_v6,
	udp_v6_only,
	tcp_v6_only)
{
	using protocol_t = decltype(TestType::protocol_v);
	typename protocol_t::socket s;
	CHECK_FALSE(s.is_open());

	auto r = TestType::make_socket();
	REQUIRE(r);
	s = std::move(*r);
	REQUIRE(s.is_open());
	CHECK(s.native_handle() != -1);

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
	}

	SECTION("assign closed")
	{
		auto new_handle = TestType::make_socket().value().release();
		pal_test::handle_guard guard{s.release()};
		REQUIRE(s.assign(TestType::protocol_v, new_handle));
		CHECK(s.native_handle() == new_handle);
		CHECK(s.native_handle() != guard.handle);
	}

	SECTION("assign invalid")
	{
		REQUIRE(s.assign(TestType::protocol_v, pal::net::__bits::invalid_native_socket));
		REQUIRE_FALSE(s.is_open());
	}

	SECTION("assign invalid to closed")
	{
		REQUIRE(s.close());
		REQUIRE(s.assign(TestType::protocol_v, pal::net::__bits::invalid_native_socket));
		REQUIRE_FALSE(s.is_open());
	}

	SECTION("assign bad_alloc")
	{
		pal_test::handle_guard guard{s.release()};
		pal_test::bad_alloc_once x;
		auto e = s.assign(TestType::protocol_v, guard.handle);
		REQUIRE_FALSE(e);
		CHECK(e.error() == std::errc::not_enough_memory);
	}

	SECTION("close")
	{
		s.close();
		CHECK_FALSE(s.is_open());
	}

	SECTION("close invalidated")
	{
		pal_test::handle_guard{s.native_handle()};
		auto e = s.close();
		REQUIRE_FALSE(e);
		CHECK(e.error() == std::errc::bad_file_descriptor);
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
