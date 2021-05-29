#include <pal/net/socket_base>
#include <pal/net/test>


namespace {


TEST_CASE("net/init")
{
	auto first = pal::net::init();
	CHECK(first);

	auto second = pal::net::init();
	CHECK(second);

	CHECK(first == second);
}


} // namespace
