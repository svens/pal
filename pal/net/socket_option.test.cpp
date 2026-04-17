#include <pal/net/socket_option.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <utility>

namespace
{

using namespace std::chrono_literals;

struct protocol_type
{
};
constexpr protocol_type protocol{};

constexpr auto test_level = pal::net::socket_option_level{100};
constexpr auto test_name = pal::net::socket_option_name{200};

TEST_CASE("net/socket_option")
{
	SECTION("int")
	{
		using option_type = pal::net::socket_option<int, test_level, test_name>;

		constexpr int init_value = 10;
		constexpr int next_value = 20;

		option_type o{init_value};
		CHECK(o.value() == init_value);
		CHECK(o.level(protocol) == test_level);
		CHECK(o.name(protocol) == test_name);

		o = next_value;
		CHECK(o.value() == next_value);
		CHECK(o.level(protocol) == test_level);
		CHECK(o.name(protocol) == test_name);

		CHECK(o.data(protocol) != nullptr);
		CHECK(std::as_const(o).data(protocol) != nullptr);

		auto sz = o.size(protocol);
		CHECK(sz == sizeof(int));

		CHECK(o.resize(protocol, sz));
		auto fail = o.resize(protocol, sz + 1);
		REQUIRE_FALSE(fail);
		CHECK(fail.error() == std::errc::invalid_argument);
	}

	SECTION("bool")
	{
		using option_type = pal::net::socket_option<bool, test_level, test_name>;

		option_type o{true};
		CHECK(o.value() == true);
		CHECK(o);
		CHECK_FALSE(!o);
		CHECK(o.level(protocol) == test_level);
		CHECK(o.name(protocol) == test_name);

		o = false;
		CHECK(o.value() == false);
		CHECK_FALSE(o);
		CHECK(!o);
		CHECK(o.level(protocol) == test_level);
		CHECK(o.name(protocol) == test_name);

		CHECK(o.data(protocol) != nullptr);
		CHECK(std::as_const(o).data(protocol) != nullptr);

		auto sz = o.size(protocol);
		CHECK(sz == sizeof(int));

		CHECK(o.resize(protocol, sz));
		auto fail = o.resize(protocol, sz + 1);
		REQUIRE_FALSE(fail);
		CHECK(fail.error() == std::errc::invalid_argument);
	}

	SECTION("linger")
	{
		constexpr auto t1 = 1s, t2 = 5s;

		pal::net::linger o{true, t1};
		CHECK(o.enabled() == true);
		CHECK(o.timeout() == t1);
		CHECK(o.level(protocol) == SOL_SOCKET);
		CHECK(o.name(protocol) == SO_LINGER);

		o.enabled(false);
		CHECK(o.enabled() == false);
		CHECK(o.timeout() == t1);

		o.timeout(t2);
		CHECK(o.enabled() == false);
		CHECK(o.timeout() == t2);

		CHECK(o.data(protocol) != nullptr);
		CHECK(std::as_const(o).data(protocol) != nullptr);

		auto sz = o.size(protocol);
		CHECK(sz == sizeof(::linger));

		CHECK(o.resize(protocol, sz));
		auto fail = o.resize(protocol, sz + 1);
		REQUIRE_FALSE(fail);
		CHECK(fail.error() == std::errc::invalid_argument);
	}

	SECTION("receive_timeout")
	{
		constexpr auto t1 = 1s, t2 = 5s;

		pal::net::receive_timeout o{t1};
		CHECK(o.timeout() == t1);
		CHECK(o.level(protocol) == SOL_SOCKET);
		CHECK(o.name(protocol) == SO_RCVTIMEO);

		o.timeout(t2);
		CHECK(o.timeout() == t2);

		CHECK(o.data(protocol) != nullptr);
		CHECK(std::as_const(o).data(protocol) != nullptr);

		auto sz = o.size(protocol);
		CHECK(sz == sizeof(pal::net::__socket::timeval));

		CHECK(o.resize(protocol, sz));
		auto fail = o.resize(protocol, sz + 1);
		REQUIRE_FALSE(fail);
		CHECK(fail.error() == std::errc::invalid_argument);
	}

	SECTION("send_timeout")
	{
		constexpr auto t1 = 1s, t2 = 5s;

		pal::net::send_timeout o{t1};
		CHECK(o.timeout() == t1);
		CHECK(o.level(protocol) == SOL_SOCKET);
		CHECK(o.name(protocol) == SO_SNDTIMEO);

		o.timeout(t2);
		CHECK(o.timeout() == t2);

		CHECK(o.data(protocol) != nullptr);
		CHECK(std::as_const(o).data(protocol) != nullptr);

		auto sz = o.size(protocol);
		CHECK(sz == sizeof(pal::net::__socket::timeval));

		CHECK(o.resize(protocol, sz));
		auto fail = o.resize(protocol, sz + 1);
		REQUIRE_FALSE(fail);
		CHECK(fail.error() == std::errc::invalid_argument);
	}
}

} // namespace
