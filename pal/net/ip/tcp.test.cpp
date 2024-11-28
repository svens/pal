#include <pal/net/ip/tcp>
#include <pal/net/test>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_template_test_macros.hpp>

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
		static_assert(v6 != pal::net::ip::tcp::v4());
		static_assert(v6 == pal::net::ip::tcp::v6());

		static_assert(v4 == v4);
		static_assert(v4 != v6);
		static_assert(v6 == v6);
		static_assert(v6 != v4);
	}
}

using namespace pal_test;

TEMPLATE_TEST_CASE("net/ip/tcp", "", tcp_v4, tcp_v6, tcp_v6_only)
{
	using protocol_t = std::remove_cvref_t<decltype(TestType::protocol_v)>;
	using endpoint_t = typename protocol_t::endpoint;

	auto acceptor = TestType::make_acceptor().value();
	endpoint_t endpoint{TestType::loopback_v, 0};
	REQUIRE(bind_next_available_port(acceptor, endpoint));
	REQUIRE_NOTHROW(acceptor.listen().value());

	auto receiver = TestType::make_socket().value();
	REQUIRE_NOTHROW(receiver.connect(endpoint).value());
	auto sender = acceptor.accept().value();

	SECTION("make_datagram_socket with endpoint")
	{
		SECTION("success")
		{
			endpoint.port(next_port(TestType::protocol_v));
			auto socket = pal::net::make_stream_socket(TestType::protocol_v, endpoint).value();
			CHECK(socket.local_endpoint().value() == endpoint);
		}

		SECTION("address in use")
		{
			auto socket = pal::net::make_stream_socket(TestType::protocol_v, endpoint);
			REQUIRE_FALSE(socket);
			CHECK(socket.error() == std::errc::address_in_use);
		}
	}
}

} // namespace
