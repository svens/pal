#include <pal/net/ip/address>
#include <pal/net/test>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

namespace {

TEST_CASE("net/ip/address")
{
	using pal::net::ip::make_address;
	using A = pal::net::ip::address;
	using A4 = pal::net::ip::address_v4;
	using A6 = pal::net::ip::address_v6;

	SECTION("constexpr")
	{
		constexpr A a;
		static_assert(std::holds_alternative<A4>(a));
		static_assert(a.is_unspecified());
		static_assert(!a.is_loopback());

		constexpr A b{A6::bytes_type{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15}};
		static_assert(std::holds_alternative<A6>(b));
		static_assert(!b.is_unspecified());
		static_assert(!b.is_loopback());

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

	auto [view, address, is_v4, is_unspecified, is_loopback] = GENERATE(
		table<std::string, A, bool, bool, bool>({
			{ "0.0.0.0",   A4::any(),      true,  true,  false },
			{ "127.0.0.1", A4::loopback(), true,  false, true  },
			{ "::",        A6::any(),      false, true,  false },
			{ "::1",       A6::loopback(), false, false, true  },
		})
	);
	CAPTURE(view);

	SECTION("copy ctor")
	{
		A a = address;
		CHECK(a == address);

		if (const auto *a4 = std::get_if<A4>(&a))
		{
			A b{*a4};
			CHECK(a == b);
		}
		else if (const auto *a6 = std::get_if<A6>(&a))
		{
			A b{*a6};
			CHECK(a == b);
		}
	}

	SECTION("copy assign")
	{
		A a, b;
		a = address;
		CHECK(a == address);
		if (const auto *a4 = std::get_if<A4>(&a))
		{
			b = *a4;
		}
		else if (const auto *a6 = std::get_if<A6>(&a))
		{
			b = *a6;
		}
		CHECK(a == b);
	}

	SECTION("properties")
	{
		if (is_v4)
		{
			CHECK(std::holds_alternative<A4>(address));
		}
		else
		{
			CHECK(std::holds_alternative<A6>(address));
		}
		CHECK(address.is_unspecified() == is_unspecified);
		CHECK(address.is_loopback() == is_loopback);
	}

	SECTION("to_chars")
	{
		char buf[pal::net::ip::address_v6::max_string_length + 1];
		auto [p, ec] = address.to_chars(buf, buf + sizeof(buf));
		REQUIRE(ec == std::errc{});
		CHECK(std::string(buf, p) == view);
	}

	SECTION("to_chars failure")
	{
		const size_t max_size = view.size(), min_size = 0;
		auto buf_size = GENERATE_COPY(range(min_size, max_size));
		std::string buf(buf_size, '\0');
		auto [p, ec] = address.to_chars(buf.data(), buf.data() + buf.size());
		CHECK(ec == std::errc::value_too_large);
		CHECK(p == buf.data() + buf.size());
	}

	SECTION("hash")
	{
		CHECK(address.hash() != 0);
	}

	SECTION("make_address(string_view)")
	{
		auto a = make_address(view);
		REQUIRE(a);
		CHECK(*a == address);
	}

	SECTION("make_address(string_view) failure")
	{
		view.append(A::max_string_length, 'x');
		auto a = make_address(view);
		REQUIRE_FALSE(a);
		CHECK(a.error() == std::errc::invalid_argument);
	}

	SECTION("format")
	{
		CHECK(std::format("{}", address) == view);
	}

	SECTION("format alternative form")
	{
		view.erase(view.find_last_of(":.") + 1) += '0';
		CHECK(std::format("{:#}", address) == view);
	}
}

} // namespace
