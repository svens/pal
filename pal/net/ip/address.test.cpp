#include <pal/net/ip/address.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

namespace
{

// NOLINTBEGIN(readability-magic-numbers)

TEST_CASE("net/ip/address")
{
	using pal::net::ip::make_address;
	using A = pal::net::ip::address;
	using A4 = pal::net::ip::address_v4;
	using A6 = pal::net::ip::address_v6;

	SECTION("constexpr")
	{
		constexpr A a;
		static_assert(a.is_v4());
		static_assert(!a.is_v6());
		static_assert(a.v4() != nullptr);
		static_assert(a.v6() == nullptr);
		static_assert(*a.v4() == A4::any);
		static_assert(a.is_unspecified());
		static_assert(!a.is_loopback());

		constexpr A b{A6::loopback};
		static_assert(b.is_v6());
		static_assert(!b.is_v4());
		static_assert(b.v6() != nullptr);
		static_assert(b.v4() == nullptr);
		static_assert(!b.is_unspecified());
		static_assert(b.is_loopback());

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
		A any_v4{A4::any};
		A any_v6{A6::any};
		A loopback_v4{A4::loopback};
		A loopback_v6{A6::loopback};

		// same-family
		CHECK(any_v4 < loopback_v4);
		CHECK(any_v4 != loopback_v4);
		CHECK(any_v6 < loopback_v6);
		CHECK(any_v6 != loopback_v6);

		// cross-family: all v4 < all v6
		CHECK(any_v4 < any_v6);
		CHECK(any_v4 < loopback_v6);
		CHECK(loopback_v4 < any_v6);
		CHECK(any_v4 != any_v6);
		CHECK_FALSE(any_v6 < any_v4);
		CHECK_FALSE(any_v6 <= any_v4);
		CHECK(any_v6 > any_v4);
		CHECK(any_v6 >= any_v4);
	}

	// clang-format off
	auto [view, addr, is_v4, is_unspecified, is_loopback] = GENERATE(
		table<std::string, A, bool, bool, bool>({
			{ "0.0.0.0",   A{A4::any},      true,  true,  false },
			{ "127.0.0.1", A{A4::loopback}, true,  false, true  },
			{ "::",        A{A6::any},      false, true,  false },
			{ "::1",       A{A6::loopback}, false, false, true  },
		})
	);
	// clang-format on
	CAPTURE(view);

	SECTION("copy ctor")
	{
		A a{addr};
		CHECK(a == addr);
	}

	SECTION("copy assign")
	{
		A a;
		a = addr;
		CHECK(a == addr);

		A b;
		if (const auto *p4 = a.v4())
		{
			b = *p4;
		}
		else
		{
			b = *a.v6();
		}
		CHECK(a == b);
	}

	SECTION("properties")
	{
		CHECK(addr.is_v4() == is_v4);
		CHECK(addr.is_v6() == !is_v4);
		CHECK((addr.v4() != nullptr) == is_v4);
		CHECK((addr.v6() != nullptr) == !is_v4);
		CHECK(addr.is_unspecified() == is_unspecified);
		CHECK(addr.is_loopback() == is_loopback);
	}

	SECTION("to_chars")
	{
		std::array<char, A::max_string_length + 1> buf{};
		auto [p, ec] = addr.to_chars(buf.data(), buf.data() + buf.size());
		REQUIRE(ec == std::errc{});
		CHECK(std::string{buf.data(), p} == view);
	}

	SECTION("to_chars failure")
	{
		const size_t max_size = view.size(), min_size = 0;
		auto buf_size = GENERATE_COPY(range(min_size, max_size));
		std::string buf(buf_size, '\0');
		auto [p, ec] = addr.to_chars(buf.data(), buf.data() + buf.size());
		CHECK(ec == std::errc::value_too_large);
		CHECK(p == buf.data() + buf.size());
	}

	SECTION("from_chars")
	{
		A a;
		auto [p, ec] = a.from_chars(view.data(), view.data() + view.size());
		REQUIRE(ec == std::errc{});
		CHECK(p == view.data() + view.size());
		CHECK(a == addr);
		CHECK(a.is_v4() == is_v4);
	}

	SECTION("from_chars failure")
	{
		auto bad = view;
		bad.back() = 'x';

		A a;
		auto [p, ec] = a.from_chars(bad.data(), bad.data() + bad.size());
		CHECK(ec == std::errc::invalid_argument);
		CHECK(p == bad.data());
	}

	SECTION("hash")
	{
		CHECK(addr.hash() != 0);
	}

	SECTION("make_address(string_view)")
	{
		auto a = make_address(view);
		REQUIRE(a);
		CHECK(*a == addr);
	}

	SECTION("make_address(string_view) failure")
	{
		auto bad = view;
		bad.append(A::max_string_length, 'x');
		auto a = make_address(bad);
		REQUIRE_FALSE(a);
		CHECK(a.error() == std::make_error_code(std::errc::invalid_argument));
	}

	SECTION("format")
	{
		CHECK(std::format("{}", addr) == view);
	}

	SECTION("masked")
	{
		if (const auto *p4 = addr.v4())
		{
			auto expected = std::format("{}", pal::masked{*p4});
			CHECK(std::format("{}", pal::masked{addr}) == expected);
		}
		else
		{
			auto expected = std::format("{}", pal::masked{*addr.v6()});
			CHECK(std::format("{}", pal::masked{addr}) == expected);
		}
	}
}

TEST_CASE("net/ip/address/make_address scope_id")
{
	using pal::net::ip::make_address;
	using A = pal::net::ip::address;

	auto a = make_address("fe80::1%5");
	REQUIRE(a);
	REQUIRE(a->is_v6());
	CHECK(a->v6()->scope_id() == 5);

	std::array<char, A::max_string_length + 1> buf{};
	auto [end, ec] = a->to_chars(buf.data(), buf.data() + buf.size());
	REQUIRE(ec == std::errc{});
	CHECK(std::string_view{buf.data(), static_cast<size_t>(end - buf.data())} == "fe80::1%5");
}

// NOLINTEND(readability-magic-numbers)

} // namespace
