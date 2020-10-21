#include <pal/net/ip/tcp>
#include <pal/net/test>


namespace {


using namespace pal_test;


TEMPLATE_TEST_CASE("net/ip/tcp", "", tcp_v4, tcp_v6)
{
	constexpr auto protocol = protocol_v<TestType>;
	std::error_code error;

	SECTION("no_delay")
	{
		socket_t<TestType> socket(protocol);

		pal::net::ip::tcp::no_delay no_delay;
		socket.get_option(no_delay, error);
		CHECK(!error);
		CHECK(no_delay.value() == false);

		no_delay = true;
		socket.set_option(no_delay, error);
		CHECK(!error);

		no_delay = false;
		socket.get_option(no_delay, error);
		CHECK(!error);
		CHECK(no_delay.value() == true);
	}
}


} // namespace
