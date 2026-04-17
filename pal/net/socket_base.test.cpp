#include <pal/net/socket_base.hpp>
#include <catch2/catch_test_macros.hpp>

namespace
{

TEST_CASE("net/init")
{
	auto first = pal::net::init();
	CHECK_NOTHROW(first.value());

	auto second = pal::net::init();
	CHECK_NOTHROW(second.value());

	CHECK(first == second);
}

TEST_CASE("net/native_socket")
{
	SECTION("default_constructed")
	{
		const pal::net::native_socket s;
		CHECK(!s);
		CHECK(s.handle() == pal::net::native_socket::invalid);
	}

	SECTION("move_construct")
	{
		auto s1 = pal::net::open(AF_INET, SOCK_STREAM, 0).value();
		auto h = s1.handle();
		CHECK(s1);

		pal::net::native_socket s2{std::move(s1)};
		CHECK(!s1);
		CHECK(s1.handle() == pal::net::native_socket::invalid);
		CHECK(s2);
		CHECK(s2.handle() == h);
	}

	SECTION("move_assign")
	{
		auto s1 = pal::net::open(AF_INET, SOCK_STREAM, 0).value();
		auto h = s1.handle();
		CHECK(s1);

		pal::net::native_socket s2;
		s2 = std::move(s1);
		CHECK(!s1);
		CHECK(s1.handle() == pal::net::native_socket::invalid);
		CHECK(s2);
		CHECK(s2.handle() == h);
	}

	SECTION("release")
	{
		auto s = pal::net::open(AF_INET, SOCK_STREAM, 0).value();
		auto h = s.handle();
		CHECK(s);

		auto released = s.release();
		CHECK(!s);
		CHECK(s.handle() == pal::net::native_socket::invalid);
		CHECK(released == h);
		pal::net::native_socket::close(released);
	}
}

} // namespace
