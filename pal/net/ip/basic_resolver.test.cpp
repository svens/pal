#include <pal/net/ip/basic_resolver>
#include <pal/net/test>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

namespace {

using namespace pal_test;
using namespace std::string_literals;

// Note: in tests below, checking host_name() == "localhost" is local setup specific
// If starts failing in GitHub Actions, find new approach

TEMPLATE_TEST_CASE("net/ip/basic_resolver", "",
	udp_v4,
	tcp_v4,
	udp_v6,
	tcp_v6)
{
	using protocol_t = std::remove_cvref_t<decltype(TestType::protocol_v)>;
	using endpoint_t = typename protocol_t::endpoint;
	using resolver_t = typename protocol_t::resolver;

	resolver_t resolver;

	SECTION("resolve")
	{
		auto result = resolver.resolve("localhost", "echo").value();
		REQUIRE_FALSE(result.empty());
		CHECK(result.max_size() > 0);
		for (auto &it: result)
		{
			CHECK(it.endpoint() == static_cast<endpoint_t>(it));
			CHECK(it.endpoint().address().is_loopback());
			CHECK(it.endpoint().port() == 7);
			CHECK(it.host_name() == "localhost");
			CHECK(it.service_name() == "echo");
		}
	}

	SECTION("resolve with protocol")
	{
		auto result = resolver.resolve(TestType::protocol_v, "localhost", "echo").value();
		REQUIRE_FALSE(result.empty());
		for (auto &it: result)
		{
			CHECK(it.endpoint().protocol() == TestType::protocol_v);
			CHECK(it.endpoint().address().is_loopback());
			CHECK(it.endpoint().port() == 7);
			CHECK(it.host_name() == "localhost");
			CHECK(it.service_name() == "echo");
		}
	}

	SECTION("resolve passive")
	{
		auto result = resolver.resolve({}, "echo", resolver.passive).value();
		REQUIRE_FALSE(result.empty());
		for (auto &it: result)
		{
			CHECK(it.endpoint().address().is_unspecified());
			CHECK(it.endpoint().port() == 7);
			CHECK(it.host_name().empty());
			CHECK(it.service_name() == "echo");
		}
	}

	SECTION("resolve passive with protocol")
	{
		auto result = resolver.resolve(TestType::protocol_v, {}, "echo", resolver.passive).value();
		REQUIRE_FALSE(result.empty());
		for (auto &it: result)
		{
			CHECK(it.endpoint().protocol() == TestType::protocol_v);
			CHECK(it.endpoint().address().is_unspecified());
			CHECK(it.endpoint().port() == 7);
			CHECK(it.host_name().empty());
			CHECK(it.service_name() == "echo");
		}
	}

	SECTION("resolve canonical name")
	{
		constexpr std::string_view host = "stun.azure.com";
		auto result = resolver.resolve(host, {}, resolver.canonical_name).value();
		REQUIRE_FALSE(result.empty());
		for (auto &it: result)
		{
			// this test makes assumptions about setup
			// if it starts failing, find another where host name != canonical name
			CHECK(it.host_name() != host);
		}
	}

	SECTION("resolve numeric")
	{
		auto host = GENERATE("127.0.0.1", "::1");
		auto result = resolver.resolve(host, "7", resolver.numeric_host | resolver.numeric_service).value();
		REQUIRE_FALSE(result.empty());
		for (auto &it: result)
		{
			CHECK(it.endpoint().address().is_loopback());
			CHECK(it.endpoint().port() == 7);
			CHECK(it.host_name() == host);
			CHECK(it.service_name() == "7");
		}
	}

	SECTION("resolve endpoint")
	{
		auto result = resolver.resolve({TestType::loopback_v, 7}).value();
		REQUIRE(result.size() == 1);
		auto it = *result.cbegin();
		CHECK_FALSE(it.host_name().empty());
		CHECK(it.service_name() == "echo");
	}

	SECTION("resolve endpoint invalid address")
	{
		sockaddr_storage blob{};
		auto result = resolver.resolve(*reinterpret_cast<const endpoint_t *>(&blob));
		REQUIRE_FALSE(result);
		CHECK(result.error().message() != "");
	}

	SECTION("resolve endpoint unknown port")
	{
		auto result = resolver.resolve({TestType::loopback_v, 65535}).value();
		REQUIRE(result.size() == 1);
		auto it = *result.cbegin();
		CHECK_FALSE(it.host_name().empty());
		CHECK(it.service_name() == "65535");
	}

	SECTION("resolve endpoint not enough memory")
	{
		pal_test::bad_alloc_once x;
		auto result = resolver.resolve({TestType::loopback_v, 7});
		REQUIRE_FALSE(result);
		CHECK(result.error() == std::errc::not_enough_memory);
	}

	SECTION("resolve numeric host invalid")
	{
		auto result = resolver.resolve("localhost", {}, resolver.numeric_host);
		REQUIRE_FALSE(result);
		CHECK(result.error() == pal::net::ip::resolver_errc::host_not_found);
		CHECK_FALSE(result.error().message().empty());
		CHECK(result.error().category().name() == "resolver"s);
	}

	SECTION("resolve numeric service invalid")
	{
		auto result = resolver.resolve({}, "echo", resolver.numeric_service);
		REQUIRE_FALSE(result);
		CHECK(result.error() == pal::net::ip::resolver_errc::service_not_found);
		CHECK_FALSE(result.error().message().empty());
		CHECK(result.error().category().name() == "resolver"s);
	}

	SECTION("resolve host not found")
	{
		auto result = resolver.resolve(pal_test::case_name(), {});
		REQUIRE_FALSE(result);
		CHECK(result.error() == pal::net::ip::resolver_errc::host_not_found);
		CHECK_FALSE(result.error().message().empty());
		CHECK(result.error().category().name() == "resolver"s);
	}

	SECTION("resolve service not found")
	{
		auto result = resolver.resolve({}, pal_test::case_name());
		REQUIRE_FALSE(result);
		CHECK(result.error() == pal::net::ip::resolver_errc::service_not_found);
		CHECK_FALSE(result.error().message().empty());
		CHECK(result.error().category().name() == "resolver"s);
	}

	SECTION("resolve not enough memory")
	{
		pal_test::bad_alloc_once x;
		auto result = resolver.resolve("localhost", "echo");
		REQUIRE_FALSE(result);
		CHECK(result.error() == std::errc::not_enough_memory);
	}

	SECTION("entry and iterator")
	{
		auto result = resolver.resolve("localhost", "echo").value();
		REQUIRE_FALSE(result.empty());

		auto it = result.begin();
		CHECK(it->endpoint().address().is_loopback());
		CHECK(it->endpoint().port() == 7);
		CHECK(it->host_name() == "localhost");
		CHECK(it->service_name() == "echo");

		CHECK(it->endpoint() == (*it).endpoint());
		CHECK(it->host_name() == (*it).host_name());
		CHECK(it->service_name() == (*it).service_name());

		auto i1 = it++;
		CHECK(it != result.cbegin());
		CHECK(i1 == result.cbegin());
		CHECK(it != i1);

		auto i2 = ++i1;
		CHECK(i1 == i2);
		CHECK(i2 == it);

		it = result.end();
		CHECK(it == result.cend());
		CHECK(it != result.begin());
		CHECK(it != result.cbegin());
	}
}

} // namespace
