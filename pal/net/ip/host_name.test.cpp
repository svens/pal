#include <pal/net/internet>
#include <pal/net/test>


namespace {


TEST_CASE("net/ip/host_name")
{
	auto n1 = pal::net::ip::host_name();
	REQUIRE(n1);

	auto n2 = pal::net::ip::host_name();
	REQUIRE(n2);

	CHECK(*n1 == *n2);
}


} // namespace
