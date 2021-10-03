#include <pal/net/ip/tcp>
#include <pal/net/async/service>
#include <pal/net/test>


namespace {


using namespace pal_test;
using namespace std::chrono_literals;


TEMPLATE_TEST_CASE("net/async/basic_socket_acceptor", "", tcp_v4, tcp_v6, tcp_v6_only)
{
	using protocol_t = std::remove_cvref_t<decltype(TestType::protocol_v)>;
	using endpoint_t = typename protocol_t::endpoint;

	auto service = pal_try(pal::net::async::make_service([](auto &&){}));

	auto acceptor = pal_try(TestType::make_acceptor());
	endpoint_t endpoint{TestType::loopback_v, 0};
	REQUIRE(pal_test::bind_next_available_port(acceptor, endpoint));

	REQUIRE_FALSE(acceptor.has_async());
	REQUIRE(service.make_async(acceptor));
	REQUIRE(acceptor.has_async());

	SECTION("make_async: multiple times")
	{
		if constexpr (!pal::assert_noexcept)
		{
			REQUIRE_THROWS_AS(service.make_async(acceptor), std::logic_error);
		}
	}
}


} // namespace
