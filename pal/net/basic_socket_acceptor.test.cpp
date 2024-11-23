#include <pal/net/basic_socket_acceptor>
#include <pal/net/test>
#include <catch2/catch_template_test_macros.hpp>

namespace {

using namespace pal_test;

TEMPLATE_TEST_CASE("net/basic_socket_acceptor", "[!nonportable]",
	tcp_v4,
	tcp_v6,
	tcp_v6_only)
{
	auto a = TestType::make_acceptor().value();
	REQUIRE(a);
	CHECK(a.native_socket()->handle != pal::net::native_socket_handle::invalid);
	CHECK(a.protocol() == TestType::protocol_v);

	SECTION("move ctor")
	{
		auto a1 = std::move(a);
		REQUIRE(a1);
		CHECK(a1.native_socket()->handle != pal::net::native_socket_handle::invalid);
		CHECK(a1.protocol() == TestType::protocol_v);
		CHECK(!a);
	}

	SECTION("move assign")
	{
		auto s_orig_handle = a.native_socket()->handle;
		auto a1 = TestType::make_acceptor().value();
		a1 = std::move(a);
		CHECK(!a);
		CHECK(a1.native_socket()->handle == s_orig_handle);
	}

	SECTION("release")
	{
		auto s_orig_handle = a.native_socket()->handle;
		auto native = a.release();
		CHECK(!a);
		CHECK(native->handle == s_orig_handle);
	}

	SECTION("make_acceptor: not_enough_memory")
	{
		pal_test::bad_alloc_once x;
		auto a1 = TestType::make_acceptor();
		REQUIRE(!a1);
		CHECK(a1.error() == std::errc::not_enough_memory);
	}
}

TEMPLATE_TEST_CASE("net/basic_socket_acceptor", "[!nonportable]",
	invalid_protocol)
{
	SECTION("make_socket_acceptor")
	{
		auto a = pal::net::make_socket_acceptor(TestType());
		REQUIRE_FALSE(a);
		CHECK(a.error() == std::errc::protocol_not_supported);
	}
}

} // namespace
