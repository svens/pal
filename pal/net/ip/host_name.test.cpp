#include <pal/net/internet>
#include <pal/net/test>


namespace {


TEST_CASE("net/ip/host_name")
{
	std::error_code error;
	auto name = pal::net::ip::host_name(error);
	CHECK(!error);
	CHECK_FALSE(name.empty());

	CHECK_NOTHROW(name = pal::net::ip::host_name());
}


} // namespace
