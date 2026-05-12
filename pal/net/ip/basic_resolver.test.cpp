#include <pal/net/ip/basic_resolver.hpp>
#include <pal/net/test.hpp>
#include <pal/test.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <ranges>

namespace
{

using namespace pal_test;
using namespace pal::net::ip;
using namespace std::string_literals;

static_assert(std::ranges::forward_range<basic_resolver_results<tcp>>);
static_assert(std::ranges::forward_range<basic_resolver_results<udp>>);

// Note: tests below that resolve "localhost"/"echo" depend on local /etc/hosts and
// /etc/services. The "echo" service maps to port 7 on POSIX. If these start failing
// in CI, a different well-known host/service pair may be needed.

TEMPLATE_TEST_CASE("net/ip/basic_resolver", "", udp_v4, tcp_v4, udp_v6, tcp_v6)
{
	using protocol_t = std::remove_cvref_t<decltype(TestType::protocol_v)>;
	using endpoint_t = protocol_t::endpoint;
	using resolver_t = protocol_t::resolver;

	resolver_t resolver;

	SECTION("resolve")
	{
		auto result = resolver.resolve("localhost", "echo").value();
		REQUIRE_FALSE(result.empty());
		CHECK(result.max_size() == 16);
		for (auto &entry: result)
		{
			CHECK(entry.endpoint() == static_cast<endpoint_t>(entry));
			CHECK(entry.endpoint().address().is_loopback());
			CHECK(entry.endpoint().port() == port_type{7});
			CHECK(entry.host_name() == "localhost");
			CHECK(entry.service_name() == "echo");
		}
	}

	SECTION("resolve with protocol")
	{
		auto result = resolver.resolve(TestType::protocol_v, "localhost", "echo").value();
		REQUIRE_FALSE(result.empty());
		for (auto &entry: result)
		{
			CHECK(entry.endpoint().protocol() == TestType::protocol_v);
			CHECK(entry.endpoint().address().is_loopback());
			CHECK(entry.endpoint().port() == port_type{7});
			CHECK(entry.host_name() == "localhost");
			CHECK(entry.service_name() == "echo");
		}
	}

	SECTION("resolve passive")
	{
		auto result = resolver.resolve({}, "echo", resolver.passive).value();
		REQUIRE_FALSE(result.empty());
		for (auto &entry: result)
		{
			CHECK(entry.endpoint().address().is_unspecified());
			CHECK(entry.endpoint().port() == port_type{7});
			CHECK(entry.host_name().empty());
			CHECK(entry.service_name() == "echo");
		}
	}

	SECTION("resolve passive with protocol")
	{
		auto result = resolver.resolve(TestType::protocol_v, {}, "echo", resolver.passive).value();
		REQUIRE_FALSE(result.empty());
		for (auto &entry: result)
		{
			CHECK(entry.endpoint().protocol() == TestType::protocol_v);
			CHECK(entry.endpoint().address().is_unspecified());
			CHECK(entry.endpoint().port() == port_type{7});
			CHECK(entry.host_name().empty());
			CHECK(entry.service_name() == "echo");
		}
	}

	SECTION("resolve canonical name", "[!nonportable]")
	{
		constexpr std::string_view host = "stun.azure.com";
		auto result = resolver.resolve(host, {}, resolver.canonical_name).value();
		REQUIRE_FALSE(result.empty());
		for (auto &entry: result)
		{
			CHECK(entry.host_name() != host);
		}
	}

	SECTION("resolve numeric")
	{
		auto host = GENERATE("127.0.0.1", "::1");
		auto result = resolver.resolve(host, "7", resolver.numeric_host | resolver.numeric_service).value();
		REQUIRE_FALSE(result.empty());
		for (auto &entry: result)
		{
			CHECK(entry.endpoint().address().is_loopback());
			CHECK(entry.endpoint().port() == port_type{7});
			CHECK(entry.host_name() == host);
			CHECK(entry.service_name() == "7");
		}
	}

	SECTION("resolve endpoint")
	{
		const endpoint_t ep{TestType::loopback_endpoint().address(), port_type{7}};
		auto result = resolver.resolve(ep).value();
		REQUIRE(result.size() == 1);
		auto entry = *result.cbegin();
		CHECK_FALSE(entry.host_name().empty());
		CHECK(entry.service_name() == "echo");
	}

	SECTION("resolve endpoint invalid address")
	{
		sockaddr_storage blob{};
		// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
		auto result = resolver.resolve(*reinterpret_cast<const endpoint_t *>(&blob));
		REQUIRE_FALSE(result);
		CHECK_FALSE(result.error().message().empty());
	}

	SECTION("resolve endpoint unknown port")
	{
		const endpoint_t ep{TestType::loopback_endpoint().address(), port_type{65535}};
		auto result = resolver.resolve(ep).value();
		REQUIRE(result.size() == 1);
		auto entry = *result.cbegin();
		CHECK_FALSE(entry.host_name().empty());
		CHECK(entry.service_name() == "65535");
	}

	SECTION("resolve endpoint not enough memory")
	{
		const pal_test::bad_alloc_once x;
		const endpoint_t ep{TestType::loopback_endpoint().address(), port_type{7}};
		auto result = resolver.resolve(ep);
		REQUIRE_FALSE(result);
		CHECK(result.error() == std::errc::not_enough_memory);
	}

	SECTION("resolve numeric host invalid")
	{
		auto result = resolver.resolve("localhost", {}, resolver.numeric_host);
		REQUIRE_FALSE(result);
		CHECK(result.error() == resolver_errc::host_not_found);
		CHECK_FALSE(result.error().message().empty());
		CHECK(result.error().category().name() == "resolver"s);
	}

	SECTION("resolve numeric service invalid")
	{
		auto result = resolver.resolve({}, "echo", resolver.numeric_service);
		REQUIRE_FALSE(result);
		CHECK(result.error() == resolver_errc::service_not_found);
		CHECK_FALSE(result.error().message().empty());
		CHECK(result.error().category().name() == "resolver"s);
	}

	SECTION("resolve host not found")
	{
		auto result = resolver.resolve(case_name(), {});
		REQUIRE_FALSE(result);
		CHECK(result.error() == resolver_errc::host_not_found);
		CHECK_FALSE(result.error().message().empty());
		CHECK(result.error().category().name() == "resolver"s);
	}

	SECTION("resolve service not found")
	{
		auto result = resolver.resolve({}, case_name());
		REQUIRE_FALSE(result);
		CHECK(result.error() == resolver_errc::service_not_found);
		CHECK_FALSE(result.error().message().empty());
		CHECK(result.error().category().name() == "resolver"s);
	}

	SECTION("resolve not enough memory")
	{
		const pal_test::bad_alloc_once x;
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
		CHECK(it->endpoint().port() == port_type{7});
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
