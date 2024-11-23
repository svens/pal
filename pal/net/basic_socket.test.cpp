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

	SECTION("make_socket: not_enough_memory")
	{
		pal_test::bad_alloc_once x;
		auto s1 = TestType::make_socket();
		REQUIRE(!s1);
		CHECK(s1.error() == std::errc::not_enough_memory);
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
