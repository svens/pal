#include <pal/net/ip/udp>
#include <pal/net/test>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_template_test_macros.hpp>

namespace {

TEST_CASE("net/ip/udp")
{
	constexpr auto v4 = pal::net::ip::udp::v4();
	constexpr auto v6 = pal::net::ip::udp::v6();

	SECTION("constexpr")
	{
		static_assert(v4.family() == AF_INET);
		static_assert(v4.type() == SOCK_DGRAM);
		static_assert(v4.protocol() == IPPROTO_UDP);
		static_assert(v4 == pal::net::ip::udp::v4());
		static_assert(v4 != pal::net::ip::udp::v6());

		static_assert(v6.family() == AF_INET6);
		static_assert(v6.type() == SOCK_DGRAM);
		static_assert(v6.protocol() == IPPROTO_UDP);
		static_assert(v6 != pal::net::ip::udp::v4());
		static_assert(v6 == pal::net::ip::udp::v6());

		static_assert(v4 == v4);
		static_assert(v4 != v6);
		static_assert(v6 == v6);
		static_assert(v6 != v4);
	}
}

} // namespace
