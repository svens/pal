#include <pal/net/socket>
#include <pal/net/test>
#include <catch2/catch_test_macros.hpp>

namespace {

TEST_CASE("net/init")
{
	auto first = pal::net::init();
	CHECK_NOTHROW(first.value());

	auto second = pal::net::init();
	CHECK_NOTHROW(second.value());

	CHECK(first == second);
}

} // namespace
