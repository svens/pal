#include <pal/net/ip/tcp>
#include <pal/net/test>


namespace {


TEST_CASE("net/ip/tcp")
{
	constexpr auto v4 = pal::net::ip::tcp::v4();
	constexpr auto v6 = pal::net::ip::tcp::v6();

	SECTION("constexpr")
	{
		static_assert(v4.family() == AF_INET);
		static_assert(v4.type() == SOCK_STREAM);
		static_assert(v4.protocol() == IPPROTO_TCP);
		static_assert(v4 == pal::net::ip::tcp::v4());
		static_assert(v4 != pal::net::ip::tcp::v6());

		static_assert(v6.family() == AF_INET6);
		static_assert(v6.type() == SOCK_STREAM);
		static_assert(v6.protocol() == IPPROTO_TCP);
		static_assert(v6 == pal::net::ip::tcp::v6());
		static_assert(v6 != pal::net::ip::tcp::v4());
	}

	SECTION("compare")
	{
		CHECK(v4 == v4);
		CHECK(v4 != v6);
		CHECK(v6 == v6);
		CHECK(v6 != v4);
	}
}


using namespace pal_test;


TEMPLATE_TEST_CASE("net/ip/tcp", "", tcp_v4, tcp_v6, tcp_v6_only)
{
}


} // namespace
