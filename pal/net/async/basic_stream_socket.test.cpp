#include <pal/net/ip/tcp>
#include <pal/net/async/service>
#include <pal/net/test>


namespace {


using namespace pal_test;
using namespace std::chrono_literals;


TEMPLATE_TEST_CASE("net/async/basic_stream_socket", "", tcp_v4, tcp_v6, tcp_v6_only)
{
	using protocol_t = std::remove_cvref_t<decltype(TestType::protocol_v)>;
	using endpoint_t = typename protocol_t::endpoint;

	auto make_service = pal::net::async::make_service([](auto &&){});
	REQUIRE(make_service);
	auto service = std::move(*make_service);

	auto socket = TestType::make_socket().value();
	endpoint_t endpoint{TestType::loopback_v, 0};
	REQUIRE(pal_test::bind_next_available_port(socket, endpoint));

	REQUIRE_FALSE(socket.has_async());
	CHECK(service.make_async(socket));
	CHECK(socket.has_async());

	SECTION("make_async: multiple times")
	{
		if constexpr (!pal::assert_noexcept)
		{
			REQUIRE_THROWS_AS(service.make_async(socket), std::logic_error);
		}
	}
}


} // namespace
