#include <pal/net/ip/host_name>
#include <pal/net/test>
#include <catch2/catch_test_macros.hpp>

namespace {

TEST_CASE("net/ip/host_name")
{
	auto n = pal::net::ip::host_name().value();
	CHECK_FALSE(n.empty());
	CHECK(n == pal::net::ip::host_name().value());
}

} // namespace
