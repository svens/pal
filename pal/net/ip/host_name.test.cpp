#include <pal/net/ip/host_name.hpp>
#include <catch2/catch_test_macros.hpp>

namespace
{

TEST_CASE("net/ip/host_name")
{
	auto name = pal::net::ip::host_name();
	REQUIRE(name);
	CHECK_FALSE(name->empty());
	CHECK(name == pal::net::ip::host_name());
}

} // namespace
