#include <pal/net/ip/address>
#include <pal/net/test>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <sstream>


namespace {


TEST_CASE("net/ip/address")
{
	using A = pal::net::ip::address;
	using A4 = pal::net::ip::address_v4;
	using A6 = pal::net::ip::address_v6;

	SECTION("constexpr")
	{
		constexpr A a;
		static_assert(a == A4::any());
		static_assert(a.is_unspecified());
		static_assert(!a.is_loopback());

		constexpr A b{A6::bytes_type{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15}};
		static_assert(!b.is_loopback());
		static_assert(!b.is_unspecified());

		constexpr A c{b};

		static_assert(b == c);
		static_assert(c != a);
		static_assert(c > a);
		static_assert(c >= a);
		static_assert(!(c < a));
		static_assert(!(c <= a));

		static_assert(a.hash() != 0);
		static_assert(b.hash() == c.hash());
	}

	SECTION("ctor")
	{
		A a;
		CHECK(a.is_v4());
		CHECK(a.is_unspecified());
	}

	SECTION("compare")
	{
		A any_v4 = A4::any(),
		  any_v6 = A6::any(),
		  loopback_v4 = A4::loopback(),
		  loopback_v6 = A6::loopback();

		CHECK_FALSE(any_v4 == loopback_v4);
		CHECK(any_v4 != loopback_v4);
		CHECK(any_v4 < loopback_v4);
		CHECK(any_v4 <= loopback_v4);
		CHECK_FALSE(any_v4 > loopback_v4);
		CHECK_FALSE(any_v4 >= loopback_v4);

		CHECK_FALSE(any_v6 == loopback_v6);
		CHECK(any_v6 != loopback_v6);
		CHECK(any_v6 < loopback_v6);
		CHECK(any_v6 <= loopback_v6);
		CHECK_FALSE(any_v6 > loopback_v6);
		CHECK_FALSE(any_v6 >= loopback_v6);

		CHECK_FALSE(any_v4 == any_v6);
		CHECK(any_v4 != any_v6);
		CHECK(any_v4 < any_v6);
		CHECK(any_v4 <= any_v6);
		CHECK_FALSE(any_v4 > any_v6);
		CHECK_FALSE(any_v4 >= any_v6);

		CHECK_FALSE(any_v6 == any_v4);
		CHECK(any_v6 != any_v4);
		CHECK_FALSE(any_v6 < any_v4);
		CHECK_FALSE(any_v6 <= any_v4);
		CHECK(any_v6 > any_v4);
		CHECK(any_v6 >= any_v4);

		CHECK_FALSE(any_v4 == loopback_v6);
		CHECK(any_v4 != loopback_v6);
		CHECK(any_v4 < loopback_v6);
		CHECK(any_v4 <= loopback_v6);
		CHECK_FALSE(any_v4 > loopback_v6);
		CHECK_FALSE(any_v4 >= loopback_v6);

		CHECK_FALSE(any_v6 == loopback_v4);
		CHECK(any_v6 != loopback_v4);
		CHECK_FALSE(any_v6 < loopback_v4);
		CHECK_FALSE(any_v6 <= loopback_v4);
		CHECK(any_v6 > loopback_v4);
		CHECK(any_v6 >= loopback_v4);
	}

	auto &[view, address, is_unspecified, is_loopback] = GENERATE(
		table<std::string, A, bool, bool>({
			{ "0.0.0.0",   A4::any(),      true,  false },
			{ "127.0.0.1", A4::loopback(), false, true  },
			{ "::",        A6::any(),      true,  false },
			{ "::1",       A6::loopback(), false, true  },
		})
	);
	CAPTURE(view);

	SECTION("copy ctor")
	{
		A a = address;
		CHECK(a == address);

		if (address.is_v4())
		{
			A b{address.v4()};
			CHECK(a == b);
		}
		else
		{
			A b{address.v6()};
			CHECK(a == b);
		}
	}

	SECTION("copy assign")
	{
		A a, b;
		a = address;
		CHECK(a == address);
		if (address.is_v4())
		{
			b = address.v4();
		}
		else
		{
			b = address.v6();
		}
		CHECK(a == b);
	}

	SECTION("properties")
	{
		CHECK(address.is_unspecified() == is_unspecified);
		CHECK(address.is_loopback() == is_loopback);
	}

	SECTION("cast")
	{
		auto r4 = address.to_v4();
		auto r6 = address.to_v6();

		if (address.is_v4())
		{
			CHECK(r4);
			REQUIRE_FALSE(r6);
			CHECK(r6.error() == std::errc::address_not_available);
		}
		else
		{
			CHECK(r6);
			REQUIRE_FALSE(r4);
			CHECK(r4.error() == std::errc::address_not_available);
		}
	}

	SECTION("to_chars")
	{
		char buf[INET6_ADDRSTRLEN];
		auto [p, ec] = address.to_chars(buf, buf + sizeof(buf));
		REQUIRE(ec == std::errc{});
		CHECK(std::string(buf, p) == view);
	}

	SECTION("to_chars failure")
	{
		const size_t max_size = address.to_string().size(), min_size = 0;
		auto buf_size = GENERATE_COPY(range(min_size, max_size));
		std::string buf(buf_size, '\0');
		auto [p, ec] = address.to_chars(buf.data(), buf.data() + buf.size());
		CHECK(ec == std::errc::value_too_large);
		CHECK(p == buf.data() + buf.size());
	}

	SECTION("to_string")
	{
		CHECK(address.to_string() == view);
	}

	SECTION("ostream")
	{
		std::ostringstream oss;
		oss << address;
		CHECK(oss.str() == view);
	}

	SECTION("hash")
	{
		CHECK(address.hash() != 0);
	}

	using pal::net::ip::make_address;
	std::string invalid_view = "xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx" + view;

	SECTION("make_address(char *)")
	{
		auto a = make_address(view.c_str());
		REQUIRE(a);
		CHECK(*a == address);
	}

	SECTION("make_address(char *) failure")
	{
		auto a = make_address(invalid_view.c_str());
		REQUIRE_FALSE(a);
		CHECK(a.error() == std::errc::invalid_argument);
	}

	SECTION("make_address(string)")
	{
		auto a = make_address(view);
		REQUIRE(a);
		CHECK(*a == address);
	}

	SECTION("make_address(string) failure")
	{
		auto a = make_address(invalid_view);
		REQUIRE_FALSE(a);
		CHECK(a.error() == std::errc::invalid_argument);
	}

	SECTION("make_address(string_view)")
	{
		auto a = make_address(std::string_view{view});
		REQUIRE(a);
		CHECK(*a == address);
	}

	SECTION("make_address(string_view) failure")
	{
		auto a = make_address(std::string_view{invalid_view});
		REQUIRE_FALSE(a);
		CHECK(a.error() == std::errc::invalid_argument);
	}
}


} // namespace
