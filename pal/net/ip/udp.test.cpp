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

using namespace pal_test;

TEMPLATE_TEST_CASE("net/ip/udp", "", udp_v4, udp_v6, udp_v6_only)
{
	using protocol_t = std::remove_cvref_t<decltype(TestType::protocol_v)>;
	using endpoint_t = typename protocol_t::endpoint;

	auto receiver = TestType::make_socket().value();
	endpoint_t endpoint{TestType::loopback_v, 0};
	REQUIRE(bind_next_available_port(receiver, endpoint));

	SECTION("make_datagram_socket with endpoint")
	{
		SECTION("success")
		{
			std::ignore = receiver.release();
			auto socket = pal::net::make_datagram_socket(TestType::protocol_v, endpoint).value();
			CHECK(socket.local_endpoint().value() == endpoint);
		}

		SECTION("address in use")
		{
			auto socket = pal::net::make_datagram_socket(TestType::protocol_v, endpoint);
			REQUIRE_FALSE(socket);
			CHECK(socket.error() == std::errc::address_in_use);
		}
	}
}

} // namespace
