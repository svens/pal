#include <pal/net/ip/basic_resolver>
#include <pal/net/test>
#include <catch2/catch_template_test_macros.hpp>


namespace {


using namespace pal_test;


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
		auto resolve = resolver.resolve("localhost", "echo");
		REQUIRE(resolve);
		CHECK(resolve->max_size() > 0);
		static_assert(resolve->max_size() > 0);
		REQUIRE_FALSE(resolve->empty());
		for (auto &it: *resolve)
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
		auto resolve = resolver.resolve(TestType::protocol_v, "localhost", "echo");
		REQUIRE(resolve);
		REQUIRE_FALSE(resolve->empty());
		for (auto &it: *resolve)
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
		auto resolve = resolver.resolve({}, "echo", resolver.passive);
		REQUIRE(resolve);
		REQUIRE_FALSE(resolve->empty());
		for (auto &it: *resolve)
		{
			CHECK(it.endpoint().address().is_unspecified());
			CHECK(it.endpoint().port() == 7);
		}
	}

	SECTION("resolve passive with protocol")
	{
		auto resolve = resolver.resolve(TestType::protocol_v, {}, "echo", resolver.passive);
		REQUIRE(resolve);
		REQUIRE_FALSE(resolve->empty());
		for (auto &it: *resolve)
		{
			CHECK(it.endpoint().protocol() == TestType::protocol_v);
			CHECK(it.endpoint().address().is_unspecified());
			CHECK(it.endpoint().port() == 7);
		}
	}

	SECTION("resolve canonical name")
	{
		constexpr std::string_view host = "stun.azure.com";
		auto resolve = resolver.resolve(host, {}, resolver.canonical_name);
		REQUIRE(resolve);
		REQUIRE_FALSE(resolve->empty());
		for (auto &it: *resolve)
		{
			// Note: this test makes assumptions about setup
			// if starts failing, find another where host name != canonical name
			CHECK(it.host_name() != host);
		}
	}

	SECTION("resolve numeric")
	{
		auto resolve = resolver.resolve("127.0.0.1", "7", resolver.numeric_host | resolver.numeric_service);
		REQUIRE(resolve);
		REQUIRE_FALSE(resolve->empty());
		for (auto &it: *resolve)
		{
			CHECK(it.endpoint() == static_cast<endpoint_t>(it));
			CHECK(it.endpoint().address().is_loopback());
			CHECK(it.endpoint().port() == 7);
			CHECK(it.host_name() == "127.0.0.1");
			CHECK(it.service_name() == "7");
		}
	}

	SECTION("resolve endpoint")
	{
		auto resolve = resolver.resolve({TestType::loopback_v, 7});
		REQUIRE(resolve);
		REQUIRE(resolve->size() == 1);
		auto entry = *resolve->begin();
		CHECK_FALSE(entry.host_name().empty());
		CHECK(entry.service_name() == "echo");
	}

	SECTION("resolve endpoint invalid address")
	{
		sockaddr_storage blob{};
		auto resolve = resolver.resolve(*reinterpret_cast<const endpoint_t *>(&blob));
		REQUIRE_FALSE(resolve);
		CHECK(resolve.error().message() != "");
	}

	SECTION("resolve endpoint unknown port")
	{
		auto resolve = resolver.resolve({TestType::loopback_v, 65535});
		REQUIRE(resolve);
		REQUIRE(resolve->size() == 1);
		auto entry = *resolve->begin();
		CHECK_FALSE(entry.host_name().empty());
		CHECK(entry.service_name() == "65535");
	}

	SECTION("resolve endpoint not enough memory")
	{
		pal_test::bad_alloc_once x;
		auto resolve = resolver.resolve({TestType::loopback_v, 7});
		REQUIRE_FALSE(resolve);
		CHECK(resolve.error() == std::errc::not_enough_memory);
	}

	SECTION("resolve numeric host invalid")
	{
		auto resolve = resolver.resolve("localhost", {}, resolver.numeric_host);
		REQUIRE_FALSE(resolve);
		CHECK(resolve.error() == pal::net::ip::resolver_errc::host_not_found);
		CHECK_FALSE(resolve.error().message().empty());
		CHECK(resolve.error().category().name() == std::string{"resolver"});
	}

	SECTION("resolve numeric service invalid")
	{
		auto resolve = resolver.resolve({}, "echo", resolver.numeric_service);
		REQUIRE_FALSE(resolve);
		CHECK(resolve.error() == pal::net::ip::resolver_errc::service_not_found);
		CHECK_FALSE(resolve.error().message().empty());
		CHECK(resolve.error().category().name() == std::string{"resolver"});
	}

	SECTION("resolve host not found")
	{
		auto resolve = resolver.resolve(pal_test::case_name(), {});
		REQUIRE_FALSE(resolve);
		CHECK(resolve.error() == pal::net::ip::resolver_errc::host_not_found);
		CHECK_FALSE(resolve.error().message().empty());
		CHECK(resolve.error().category().name() == std::string{"resolver"});
	}

	SECTION("resolve service not found")
	{
		auto resolve = resolver.resolve({}, pal_test::case_name());
		REQUIRE_FALSE(resolve);
		CHECK(resolve.error() == pal::net::ip::resolver_errc::service_not_found);
		CHECK_FALSE(resolve.error().message().empty());
		CHECK(resolve.error().category().name() == std::string{"resolver"});
	}

	SECTION("resolve not enough memory")
	{
		pal_test::bad_alloc_once x;
		auto resolve = resolver.resolve("localhost", "echo");
		REQUIRE_FALSE(resolve);
		CHECK(resolve.error() == std::errc::not_enough_memory);
	}

	SECTION("entry and iterator")
	{
		auto resolve = resolver.resolve("localhost", "echo");
		REQUIRE(resolve);
		REQUIRE_FALSE(resolve->empty());

		auto it = resolve->begin();
		CHECK(it->endpoint().address().is_loopback());
		CHECK(it->endpoint().port() == 7);
		CHECK(it->host_name() == "localhost");
		CHECK(it->service_name() == "echo");

		CHECK(it->endpoint() == (*it).endpoint());
		CHECK(it->host_name() == (*it).host_name());
		CHECK(it->service_name() == (*it).service_name());

		auto i1 = it++;
		CHECK(it != resolve->cbegin());
		CHECK(i1 == resolve->cbegin());
		CHECK(i1 != it);

		auto i2 = ++i1;
		CHECK(i1 == i2);
		CHECK(i2 == it);

		it = resolve->end();
		CHECK(it == resolve->cend());
		CHECK(it != resolve->begin());
		CHECK(it != resolve->cbegin());
	}

	SECTION("empty basic_resolver_results")
	{
		pal::net::ip::basic_resolver_results<protocol_t> results;
		REQUIRE(results.empty());
		CHECK(results.begin() == results.end());
		CHECK(results.cbegin() == results.cend());
	}

	SECTION("swap")
	{
		auto echo = resolver.resolve({}, "echo");
		REQUIRE(echo);
		REQUIRE_FALSE(echo->empty());
		CHECK(echo->begin()->endpoint().port() == 7);

		auto time = resolver.resolve({}, "time");
		REQUIRE(time);
		REQUIRE_FALSE(time->empty());
		CHECK(time->begin()->endpoint().port() == 37);

		echo->swap(*time);
		CHECK(echo->begin()->endpoint().port() == 37);
		CHECK(time->begin()->endpoint().port() == 7);
	}
}


} // namespace
