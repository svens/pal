#include <pal/net/basic_socket_acceptor>
#include <pal/net/test>


namespace {


using namespace pal_test;


TEMPLATE_TEST_CASE("net/basic_socket_acceptor", "",
	tcp_v4,
	tcp_v6,
	tcp_v6_only)
{
	using protocol_t = decltype(TestType::protocol_v);
	typename protocol_t::acceptor a;
	CHECK_FALSE(a.is_open());

	auto r = TestType::make_acceptor();
	REQUIRE(r);
	a = std::move(*r);
	REQUIRE(a.is_open());
	CHECK(a.native_handle() != -1);

	SECTION("move ctor")
	{
		auto a1 = std::move(a);
		CHECK(a1.is_open());
		CHECK_FALSE(a.is_open());
	}

	SECTION("move assign")
	{
		std::decay_t<decltype(a)> a1;
		a1 = std::move(a);
		CHECK(a1.is_open());
		CHECK_FALSE(a.is_open());
	}

	SECTION("release")
	{
		pal_test::handle_guard guard{a.release()};
		CHECK_FALSE(a.is_open());
	}

	SECTION("open")
	{
		pal_test::handle_guard guard{a.release()};
		REQUIRE_FALSE(a.is_open());
		REQUIRE(a.open(TestType::protocol_v));
		CHECK(a.is_open());
	}

	SECTION("open already opened")
	{
		if constexpr (!pal::assert_noexcept)
		{
			REQUIRE_THROWS_AS(a.open(TestType::protocol_v), std::logic_error);
		}
		else
		{
			// although will succeed in release build, it is still runtime
			// penalty for compile-time logic error
			auto handle = a.native_handle();
			REQUIRE(a.open(TestType::protocol_v));
			CHECK(handle != a.native_handle());
		}
	}

	SECTION("assign opened")
	{
		auto new_handle = TestType::make_acceptor().value().release();
		auto old_handle = a.native_handle();
		REQUIRE(a.assign(TestType::protocol_v, new_handle));
		CHECK(a.native_handle() == new_handle);
		CHECK(a.native_handle() != old_handle);
	}

	SECTION("assign closed")
	{
		auto new_handle = TestType::make_acceptor().value().release();
		pal_test::handle_guard guard{a.release()};
		REQUIRE(a.assign(TestType::protocol_v, new_handle));
		CHECK(a.native_handle() == new_handle);
		CHECK(a.native_handle() != guard.handle);
	}

	SECTION("assign invalid")
	{
		REQUIRE(a.assign(TestType::protocol_v, pal::net::__bits::invalid_native_socket));
		REQUIRE_FALSE(a.is_open());
	}

	SECTION("assign invalid to closed")
	{
		REQUIRE(a.close());
		REQUIRE(a.assign(TestType::protocol_v, pal::net::__bits::invalid_native_socket));
		REQUIRE_FALSE(a.is_open());
	}

	SECTION("assign bad_alloc")
	{
		pal_test::handle_guard guard{a.release()};
		pal_test::bad_alloc_once x;
		auto e = a.assign(TestType::protocol_v, guard.handle);
		REQUIRE_FALSE(e);
		CHECK(e.error() == std::errc::not_enough_memory);
	}

	SECTION("close")
	{
		a.close();
		CHECK_FALSE(a.is_open());
	}

	SECTION("close invalidated")
	{
		pal_test::handle_guard{a.native_handle()};
		auto e = a.close();
		REQUIRE_FALSE(e);
		CHECK(e.error() == std::errc::bad_file_descriptor);
	}

	SECTION("close already closed")
	{
		a.close();
		CHECK_FALSE(a.is_open());

		if constexpr (!pal::assert_noexcept)
		{
			REQUIRE_THROWS_AS(a.close(), std::logic_error);
		}
	}

	SECTION("make bad_alloc")
	{
		pal_test::handle_guard guard{a.release()};
		pal_test::bad_alloc_once x;
		r = TestType::make_acceptor();
		REQUIRE_FALSE(r);
		CHECK(r.error() == std::errc::not_enough_memory);
	}
}


TEMPLATE_TEST_CASE("net/basic_socket_acceptor", "",
	invalid_protocol)
{
	SECTION("make_socket_acceptor")
	{
		auto acceptor = pal::net::make_socket_acceptor(TestType());
		REQUIRE_FALSE(acceptor);
		CHECK(acceptor.error() == std::errc::protocol_not_supported);
	}
}


} // namespace
