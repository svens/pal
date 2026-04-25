#include <pal/net/ip/address_v4.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#if __has_include(<arpa/inet.h>)
	#include <arpa/inet.h>
#elif __has_include(<ws2tcpip.h>)
	#include <ws2tcpip.h>
#endif

namespace
{

// Independent reference parser for cross-checking
uint32_t to_host_uint (const std::string &src)
{
	::in_addr dest{};
	::inet_pton(AF_INET, src.c_str(), &dest);
	return ntohl(static_cast<uint32_t>(dest.s_addr));
}

TEST_CASE("net/ip/address_v4")
{
	using pal::net::ip::make_address_v4;

	using A = pal::net::ip::address_v4;
	using B = A::bytes_type;

	SECTION("constexpr")
	{
		constexpr A a;
		static_assert(a == A::any);
		static_assert(a.is_unspecified());
		static_assert(!a.is_loopback());
		static_assert(!a.is_private());

		constexpr A b{B{127, 0, 0, 1}};
		static_assert(b == A::loopback);
		static_assert(b.is_loopback());
		static_assert(!b.is_unspecified());
		static_assert(!b.is_private());

		constexpr A c{b.to_uint()};
		constexpr A d{b.to_bytes()};
		static_assert(c == d);

		constexpr A e{b};
		static_assert(e == b);
		static_assert(e != a);
		static_assert(e > a);
		static_assert(e >= a);
		static_assert(!(e < a));
		static_assert(!(e <= a));

		static_assert(A::any.is_unspecified());
		static_assert(A::loopback.is_loopback());
	}

	SECTION("ctor")
	{
		const A a;
		CHECK(a.to_uint() == 0);
	}

	SECTION("copy ctor")
	{
		A a;
		auto a1 = a;
		CHECK(a1 == a);
	}

	SECTION("compare")
	{
		auto a = A::loopback;
		auto b = A::broadcast;
		auto c = a;

		CHECK_FALSE(a == b);
		CHECK_FALSE(b == a);
		CHECK(a == c);

		CHECK(a != b);
		CHECK(b != a);
		CHECK_FALSE(a != c);

		CHECK(a < b);
		CHECK_FALSE(b < a);
		CHECK_FALSE(a < c);

		CHECK_FALSE(a > b);
		CHECK(b > a);
		CHECK_FALSE(a > c);

		CHECK(a <= b);
		CHECK_FALSE(b <= a);
		CHECK(a <= c);

		CHECK_FALSE(a >= b);
		CHECK(b >= a);
		CHECK(a >= c);
	}

	// clang-format off
	auto [view, bytes, is_unspecified, is_loopback, is_private] = GENERATE(
		table<std::string, B, bool, bool, bool>({
			{ "0.0.0.0",         B{0,0,0,0},         true,  false, false },
			{ "0.1.0.0",         B{0,1,0,0},         false, false, false },
			{ "0.0.1.0",         B{0,0,1,0},         false, false, false },
			{ "0.0.0.1",         B{0,0,0,1},         false, false, false },
			{ "127.0.0.1",       B{127,0,0,1},       false, true,  false },
			{ "255.255.255.255", B{255,255,255,255}, false, false, false },
			{ "224.1.2.3",       B{224,1,2,3},       false, false, false },
			{ "10.1.2.3",        B{10,1,2,3},        false, false, true  },
			{ "172.15.0.1",      B{172,15,0,1},      false, false, false },
			{ "172.16.0.1",      B{172,16,0,1},      false, false, true  },
			{ "172.20.0.1",      B{172,20,0,1},      false, false, true  },
			{ "172.31.255.255",  B{172,31,255,255},  false, false, true  },
			{ "172.32.0.1",      B{172,32,0,1},      false, false, false },
			{ "192.168.1.2",     B{192,168,1,2},     false, false, true  },
			{ "192.169.0.1",     B{192,169,0,1},     false, false, false },
		})
	);
	// clang-format on
	CAPTURE(view);

	A a{bytes};
	auto as_uint = to_host_uint(view);

	SECTION("ctor(Arg)")
	{
		A a1{as_uint};
		CHECK(a1.to_uint() == as_uint);
		CHECK(a1.to_bytes() == bytes);
		CHECK(a1 == a);
	}

	SECTION("properties")
	{
		CHECK(a.is_unspecified() == is_unspecified);
		CHECK(a.is_loopback() == is_loopback);
		CHECK(a.is_private() == is_private);
	}

	SECTION("hash")
	{
		CHECK(a.hash() != 0);
	}

	SECTION("to_chars")
	{
		std::array<char, A::max_string_length + 1> buf{};
		auto [p, ec] = a.to_chars(buf.data(), buf.data() + buf.size());
		REQUIRE(ec == std::errc{});
		CHECK(std::string{buf.data(), p} == view);
	}

	SECTION("to_chars failure")
	{
		const size_t max_size = view.size(), min_size = 0;
		auto buf_size = GENERATE_COPY(range(min_size, max_size));
		std::string buf(buf_size, '\0');
		auto [p, ec] = a.to_chars(buf.data(), buf.data() + buf.size());
		CHECK(ec == std::errc::value_too_large);
		CHECK(p == buf.data() + buf.size());
	}

	SECTION("from_chars")
	{
		A b;
		auto [p, ec] = b.from_chars(view.data(), view.data() + view.size());
		REQUIRE(ec == std::errc{});
		CHECK(p == view.data() + view.size());
		CHECK(a == b);
	}

	SECTION("from_chars streaming")
	{
		const auto input = view + "/24";
		A b;
		auto [p, ec] = b.from_chars(input.data(), input.data() + input.size());
		REQUIRE(ec == std::errc{});
		CHECK(*p == '/');
		CHECK(b == a);
	}

	SECTION("from_chars failure")
	{
		const auto bad = 'x' + view;
		A b;
		const auto r = b.from_chars(bad.data(), bad.data() + bad.size());
		REQUIRE(r.ec == std::errc::invalid_argument);
		CHECK(r.ptr == bad.data());
	}

	SECTION("from_chars invalid address")
	{
		// valid chars consumed but inet_pton rejects — ptr advances past them
		const std::string bad = "999.0.0.0";
		A b;
		const auto r = b.from_chars(bad.data(), bad.data() + bad.size());
		CHECK(r.ec == std::errc::invalid_argument);
		CHECK(r.ptr == bad.data() + bad.size());
	}

	SECTION("make_address_v4(bytes_type)")
	{
		CHECK(a == make_address_v4(bytes));
	}

	SECTION("make_address_v4(uint_type)")
	{
		CHECK(a == make_address_v4(as_uint));
	}

	SECTION("make_address_v4(string_view)")
	{
		auto v = make_address_v4(view);
		REQUIRE(v);
		CHECK(*v == a);
	}

	SECTION("make_address_v4(string_view) failure")
	{
		const auto s = view + 'x';
		CHECK_FALSE(make_address_v4(std::string_view{s}));
	}

	SECTION("format")
	{
		CHECK(std::format("{}", a) == view);
	}

	SECTION("masked")
	{
		auto expected = view.substr(0, view.rfind('.') + 1) + "0";
		CHECK(std::format("{}", pal::masked{a}) == expected);
	}
}

} // namespace
