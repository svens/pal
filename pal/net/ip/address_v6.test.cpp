#include <pal/net/ip/address_v6>
#include <pal/net/test>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <sstream>

#if __has_include(<arpa/inet.h>)
	#include <arpa/inet.h>
#elif __has_include(<ws2tcpip.h>)
	#include <ws2tcpip.h>
#endif

namespace {

TEST_CASE("net/ip/address_v6")
{
	using pal::net::ip::make_address_v4;
	using pal::net::ip::make_address_v6;
	using pal::net::ip::v4_mapped;

	using A = pal::net::ip::address_v6;
	using B = A::bytes_type;

	SECTION("constexpr")
	{
		constexpr A a;
		static_assert(a == A::any());
		static_assert(a.is_unspecified());
		static_assert(!a.is_loopback());

		constexpr A b{B{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15}};
		static_assert(!b.is_loopback());
		static_assert(!b.is_unspecified());

		constexpr A c{b.to_bytes()};
		static_assert(b == c);
		static_assert(c != a);
		static_assert(c > a);
		static_assert(c >= a);
		static_assert(!(c < a));
		static_assert(!(c <= a));

		static_assert(A::any().is_unspecified());
		static_assert(A::loopback().is_loopback());

		static_assert(a.hash() != 0);
		static_assert(b.hash() == c.hash());
	}

	SECTION("ctor")
	{
		A a;
		CHECK(a.is_unspecified());
	}

	SECTION("copy ctor")
	{
		A a;
		auto a1 = a;
		CHECK(a1 == a);
	}

	SECTION("compare")
	{
		auto a = A::any();
		auto b = A::loopback();
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

	auto [view, bytes, is_unspecified, is_loopback, is_v4_mapped] = GENERATE(
		table<std::string, B, bool, bool, bool>({
			{ "::", A::any().to_bytes(), true, false, false },
			{ "::1", A::loopback().to_bytes(), false, true, false },
			{ "::ffff:1.2.3.4", B{0,0,0,0,0,0,0,0,0,0,0xff,0xff,0x01,0x02,0x03,0x04}, false, false, true },
			{ "1:203:405:607:809:a0b:c0d:e0f", B{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15}, false, false, false },

			// for branch coverage
			{ "fe80::1", B{0xfe,0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01}, false, false, false },
			{ "fec0::1", B{0xfe,0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01}, false, false, false },
			{ "100::ffff:0:0", B{0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0x00,0x00,0x00,0x00}, false, false, false },
			{ "1::ffff:0:0", B{0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0x00,0x00,0x00,0x00}, false, false, false },
			{ "0:100::ffff:0:0", B{0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0x00,0x00,0x00,0x00}, false, false, false },
			{ "0:1::ffff:0:0", B{0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0x00,0x00,0x00,0x00}, false, false, false },
			{ "::100:0:0:ffff:0:0", B{0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0x00,0x00,0x00,0x00}, false, false, false },
			{ "::1:0:0:ffff:0:0", B{0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0xff,0xff,0x00,0x00,0x00,0x00}, false, false, false },
			{ "::100:0:ffff:0:0", B{0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0xff,0xff,0x00,0x00,0x00,0x00}, false, false, false },
			{ "::1:0:ffff:0:0", B{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0xff,0xff,0x00,0x00,0x00,0x00}, false, false, false },
			{ "::100:ffff:0:0", B{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0xff,0xff,0x00,0x00,0x00,0x00}, false, false, false },
			{ "::1:ffff:0:0", B{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0xff,0xff,0x00,0x00,0x00,0x00}, false, false, false },
			{ "::fffe:0:0", B{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xfe,0x00,0x00,0x00,0x00}, false, false, false },
			{ "ff00::1", B{0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01}, false, false, false },
			{ "ff01::1", B{0xff,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01}, false, false, false },
			{ "ff02::1", B{0xff,0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01}, false, false, false },
			{ "ff05::1", B{0xff,0x05,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01}, false, false, false },
			{ "ff08::1", B{0xff,0x08,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01}, false, false, false },
			{ "ff0e::1", B{0xff,0x0e,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01}, false, false, false },
			{ "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff", B{0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff}, false, false, false },
		})
	);
	CAPTURE(view);

	A a{bytes};
	CHECK(a.to_bytes() == bytes);

	SECTION("properties")
	{
		CHECK(a.is_unspecified() == is_unspecified);
		CHECK(a.is_loopback() == is_loopback);
		CHECK(a.is_v4_mapped() == is_v4_mapped);
	}

	SECTION("hash")
	{
		CHECK(a.hash() != 0);
	}

	SECTION("to_chars")
	{
		char buf[INET6_ADDRSTRLEN];
		auto [p, ec] = a.to_chars(buf, buf + sizeof(buf));
		REQUIRE(ec == std::errc{});
		CHECK(std::string{buf, p} == view);
	}

	SECTION("to_chars failure")
	{
		const size_t max_size = a.to_string().size(), min_size = 0;
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

	SECTION("from_chars failure")
	{
		view.back() = 'x';

		A b;
		auto [p, ec] = b.from_chars(view.data(), view.data() + view.size());
		REQUIRE(ec == std::errc::invalid_argument);
		CHECK(p == view.data());
	}

	SECTION("to_string")
	{
		CHECK(a.to_string() == view);
	}

	SECTION("ostream")
	{
		std::ostringstream oss;
		oss << a;
		CHECK(oss.str() == view);
	}

	SECTION("make_address_v6(bytes_type)")
	{
		CHECK(a == make_address_v6(bytes));
	}

	SECTION("make_address_v6(string_view)")
	{
		auto v = make_address_v6(view);
		REQUIRE(v);
		CHECK(*v == a);
	}

	SECTION("make_address_v6(string_view) failure")
	{
		view.append(A::max_string_length, 'x');
		auto v = make_address_v6(view);
		REQUIRE_FALSE(v);
		CHECK(v.error() == std::errc::invalid_argument);
	}

	SECTION("v4_mapped")
	{
		auto v4 = make_address_v4(v4_mapped, a);
		if (a.is_v4_mapped())
		{
			REQUIRE(v4);
			auto v6 = make_address_v6(v4_mapped, *v4);
			CHECK(v6 == a);
		}
		else
		{
			REQUIRE_FALSE(v4);
			CHECK(v4.error() == std::errc::invalid_argument);
		}
	}
}

} // namespace
