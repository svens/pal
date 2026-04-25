#include <pal/net/ip/network_v6.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

namespace
{

using pal::net::ip::make_network_v6;
using pal::net::ip::network_v6;

using A = pal::net::ip::address_v6;
using B = A::bytes_type;

TEST_CASE("net/ip/network_v6")
{
	SECTION("constexpr")
	{
		// /0: contains everything
		constexpr auto n0 = make_network_v6(A::any, 0);
		static_assert(n0.has_value());
		static_assert(n0->address() == A::any);
		static_assert(n0->prefix_length() == 0);
		static_assert(n0->netmask() == A::any);
		static_assert(!n0->is_host());
		static_assert(n0->contains(A::any));
		static_assert(n0->contains(A::loopback));

		// /128: host route
		constexpr auto n128 = make_network_v6(A::loopback, 128);
		static_assert(n128.has_value());
		static_assert(n128->is_host());
		static_assert(n128->contains(A::loopback));
		static_assert(!n128->contains(A::any));

		// canonicalization at compile time: fe80::1/10 -> fe80::/10
		constexpr auto nc = make_network_v6(B{0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}, 10);
		static_assert(nc.has_value());
		static_assert(nc->address() == B{0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});

		// is_subnet_of
		constexpr auto n32 = make_network_v6(B{0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 32);
		constexpr auto n48 = make_network_v6(B{0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 48);
		static_assert(n32.has_value() && n48.has_value());
		static_assert(n48->is_subnet_of(*n32));
		static_assert(!n32->is_subnet_of(*n48));
		static_assert(n32->is_subnet_of(*n32));

		// comparison
		static_assert(*n32 < *n48);
		static_assert(*n32 != *n48);
	}

	SECTION("compare")
	{
		auto a = *make_network_v6("2001:db8::/32");
		auto b = *make_network_v6("2001:db8::/48");
		auto c = *make_network_v6("fe80::/10");
		auto d = *make_network_v6("2001:db8::/32");

		CHECK(a == d);
		CHECK(a != b);
		CHECK(a < b);
		CHECK(b < c);
		CHECK(c > a);
		CHECK(d <= a);
		CHECK(d >= a);
	}

	SECTION("is_subnet_of")
	{
		auto n32 = *make_network_v6("2001:db8::/32");
		auto n48 = *make_network_v6("2001:db8::/48");
		auto n64 = *make_network_v6("2001:db8::/64");
		auto unrelated = *make_network_v6("fe80::/10");
		auto all = *make_network_v6("::/0");

		CHECK(n64.is_subnet_of(n32));
		CHECK(n64.is_subnet_of(n48));
		CHECK(n48.is_subnet_of(n32));
		CHECK(n64.is_subnet_of(n64)); // reflexive
		CHECK(n32.is_subnet_of(all));

		CHECK_FALSE(n32.is_subnet_of(n48));
		CHECK_FALSE(n32.is_subnet_of(n64));
		CHECK_FALSE(n32.is_subnet_of(unrelated));
		CHECK_FALSE(unrelated.is_subnet_of(n32));
	}

	SECTION("failures")
	{
		SECTION("invalid prefix")
		{
			CHECK_FALSE(make_network_v6(A::any, 129));
			CHECK_FALSE(make_network_v6(A::any, 255));
		}

		SECTION("invalid string")
		{
			CHECK_FALSE(make_network_v6(""));
			CHECK_FALSE(make_network_v6("::/"));
			CHECK_FALSE(make_network_v6("::/129"));
			CHECK_FALSE(make_network_v6("::/0 "));
			CHECK_FALSE(make_network_v6("bad/0"));
		}

		SECTION("from_chars")
		{
			auto check_fail = [] (std::string_view s)
			{
				network_v6 n;
				CHECK(n.from_chars(s.data(), s.data() + s.size()).ec == std::errc::invalid_argument);
			};
			check_fail("");
			check_fail("::/");
			check_fail("::/129");
			check_fail("bad/0");
		}
	}

	SECTION("scope_id dropped")
	{
		// scope_id on the address is silently dropped; canonical form has none
		network_v6 n;
		const std::string input = "fe80::1%5/64,next";
		auto [p, ec] = n.from_chars(input.data(), input.data() + input.size());
		REQUIRE(ec == std::errc{});
		CHECK(*p == ',');
		CHECK(n.address().scope_id() == 0);
		CHECK(n.prefix_length() == 64);
		CHECK(std::format("{}", n) == "fe80::/64");
	}

	struct case_t
	{
		std::string text;
		A addr;
		uint8_t prefix_len;
		A mask;
		bool host = false;
		A inside;
		A outside;
	};

	// clang-format off
	auto expected = GENERATE(values<case_t>({
		{
			.text = "::/0",
			.addr = A::any,
			.prefix_len = 0,
			.mask = A::any,
			.inside = A::loopback,
			.outside = A::any
		},
		{
			.text = "2001:db8::/32",
			.addr = B{0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
			.prefix_len = 32,
			.mask = B{0xff, 0xff, 0xff, 0xff, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
			.inside = B{0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
			.outside = B{0x20, 0x01, 0x0d, 0xb9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
		},
		{
			.text = "fc00::/7",
			.addr = B{0xfc, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
			.prefix_len = 7,
			.mask = B{0xfe, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
			.inside = B{0xfd, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
			.outside = B{0xfe, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
		},
		{
			.text = "fe80::/10",
			.addr = B{0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
			.prefix_len = 10,
			.mask = B{0xff, 0xc0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
			.inside = B{0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
			.outside = B{0xfe, 0xc0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
		},
		{
			.text = "::1/128",
			.addr = A::loopback,
			.prefix_len = 128,
			.mask = B{0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff},
			.host = true,
			.inside = A::loopback,
			.outside = A::any
		},
	}));
	// clang-format on

	CAPTURE(expected.text);

	SECTION("make_network_v6(address_v6, prefix_len)")
	{
		auto n = make_network_v6(expected.addr, expected.prefix_len);
		REQUIRE(n);
		CHECK(n->address() == expected.addr);
		CHECK(n->prefix_length() == expected.prefix_len);
	}

	SECTION("make_network_v6(string_view)")
	{
		auto n = make_network_v6(expected.text);
		REQUIRE(n);
		CHECK(n->address() == expected.addr);
		CHECK(n->prefix_length() == expected.prefix_len);

		if (expected.host)
		{
			auto n2 = make_network_v6(std::format("{}", expected.addr));
			REQUIRE(n2);
			CHECK(*n2 == *n);
		}
	}

	SECTION("canonicalization")
	{
		auto n = make_network_v6(expected.inside, expected.prefix_len);
		REQUIRE(n);
		CHECK(n->address() == expected.addr);
	}

	SECTION("properties")
	{
		auto n = *make_network_v6(expected.addr, expected.prefix_len);
		CHECK(n.netmask() == expected.mask);
		CHECK(n.is_host() == expected.host);
	}

	SECTION("contains")
	{
		auto n = *make_network_v6(expected.addr, expected.prefix_len);
		CHECK(n.contains(expected.addr));
		CHECK(n.contains(expected.inside));
		if (expected.prefix_len > 0)
		{
			CHECK_FALSE(n.contains(expected.outside));
		}
	}

	SECTION("hash")
	{
		auto n = *make_network_v6(expected.addr, expected.prefix_len);
		CHECK(n.hash() != 0);
	}

	SECTION("format")
	{
		auto n = *make_network_v6(expected.addr, expected.prefix_len);
		CHECK(std::format("{}", n) == expected.text);
	}

	SECTION("to_chars")
	{
		auto n = *make_network_v6(expected.addr, expected.prefix_len);
		std::array<char, network_v6::max_string_length + 1> buf{};
		auto [p, ec] = n.to_chars(buf.data(), buf.data() + buf.size());
		REQUIRE(ec == std::errc{});
		CHECK(std::string_view{buf.data(), p} == expected.text);
	}

	SECTION("to_chars failure")
	{
		auto n = *make_network_v6(expected.addr, expected.prefix_len);
		const size_t max_size = expected.text.size(), min_size = 0;
		auto buf_size = GENERATE_COPY(range(min_size, max_size));
		std::string buf(buf_size, '\0');
		auto [p, ec] = n.to_chars(buf.data(), buf.data() + buf.size());
		CHECK(ec == std::errc::value_too_large);
		CHECK(p == buf.data() + buf.size());
	}

	SECTION("from_chars")
	{
		network_v6 n;
		auto [p, ec] = n.from_chars(expected.text.data(), expected.text.data() + expected.text.size());
		REQUIRE(ec == std::errc{});
		CHECK(p == expected.text.data() + expected.text.size());
		CHECK(n == *make_network_v6(expected.addr, expected.prefix_len));
	}

	SECTION("from_chars streaming")
	{
		const auto input = expected.text + ",next";
		network_v6 n;
		auto [p, ec] = n.from_chars(input.data(), input.data() + input.size());
		REQUIRE(ec == std::errc{});
		CHECK(*p == ',');
		CHECK(n == *make_network_v6(expected.addr, expected.prefix_len));
	}

	SECTION("from_chars streaming address-only")
	{
		const auto input = std::format("{},next", expected.addr);
		network_v6 n;
		auto [p, ec] = n.from_chars(input.data(), input.data() + input.size());
		REQUIRE(ec == std::errc{});
		CHECK(*p == ',');
		CHECK(n.prefix_length() == network_v6::max_prefix_length);
		CHECK(n.address() == expected.addr);
	}

	SECTION("from_chars failure")
	{
		const auto bad = 'x' + expected.text;
		network_v6 n;
		const auto r = n.from_chars(bad.data(), bad.data() + bad.size());
		REQUIRE(r.ec == std::errc::invalid_argument);
		CHECK(r.ptr == bad.data());
	}
}

} // namespace
