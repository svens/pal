#include <pal/net/ip/network_v4.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

namespace
{

// NOLINTBEGIN(readability-magic-numbers)

using pal::net::ip::make_network_v4;
using pal::net::ip::network_v4;

using A = pal::net::ip::address_v4;
using B = A::bytes_type;

TEST_CASE("net/ip/network_v4")
{
	SECTION("constexpr")
	{
		// /0: contains everything
		constexpr auto n0 = make_network_v4(A::any, 0);
		static_assert(n0.has_value());
		static_assert(n0->address() == A::any);
		static_assert(n0->prefix_length() == 0);
		static_assert(n0->netmask() == A::any);
		static_assert(n0->broadcast() == A::broadcast);
		static_assert(!n0->is_host());
		static_assert(n0->contains(A::any));
		static_assert(n0->contains(A::loopback));
		static_assert(n0->contains(A::broadcast));

		// /32: host route
		constexpr auto n32 = make_network_v4(A::loopback, 32);
		static_assert(n32.has_value());
		static_assert(n32->is_host());
		static_assert(n32->netmask() == A::broadcast);
		static_assert(n32->broadcast() == A::loopback);
		static_assert(n32->contains(A::loopback));
		static_assert(!n32->contains(A::any));

		// canonicalization at compile time
		constexpr auto nc = make_network_v4(B{192, 168, 1, 5}, 24);
		static_assert(nc.has_value());
		static_assert(nc->address() == B{192, 168, 1, 0});

		// is_subnet_of
		constexpr auto n8 = make_network_v4(B{10, 0, 0, 0}, 8);
		constexpr auto n16 = make_network_v4(B{10, 0, 0, 0}, 16);
		static_assert(n8.has_value() && n16.has_value());
		static_assert(n16->is_subnet_of(*n8));
		static_assert(!n8->is_subnet_of(*n16));
		static_assert(n8->is_subnet_of(*n8));

		// comparison
		static_assert(*n8 < *n16);
		static_assert(*n8 != *n16);
	}

	SECTION("compare")
	{
		auto a = *make_network_v4("10.0.0.0/8");
		auto b = *make_network_v4("10.0.0.0/16");
		auto c = *make_network_v4("192.168.0.0/16");
		auto d = *make_network_v4("10.0.0.0/8");

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
		auto n8 = *make_network_v4("10.0.0.0/8");
		auto n16 = *make_network_v4("10.0.0.0/16");
		auto n24 = *make_network_v4("10.0.0.0/24");
		auto unrelated = *make_network_v4("192.168.0.0/16");
		auto all = *make_network_v4("0.0.0.0/0");

		CHECK(n24.is_subnet_of(n8));
		CHECK(n24.is_subnet_of(n16));
		CHECK(n16.is_subnet_of(n8));
		CHECK(n24.is_subnet_of(n24)); // reflexive
		CHECK(n8.is_subnet_of(all));

		CHECK_FALSE(n8.is_subnet_of(n16));
		CHECK_FALSE(n8.is_subnet_of(n24));
		CHECK_FALSE(n8.is_subnet_of(unrelated));
		CHECK_FALSE(unrelated.is_subnet_of(n8));
	}

	SECTION("failures")
	{
		SECTION("invalid prefix")
		{
			CHECK_FALSE(make_network_v4(A::any, 33));
			CHECK_FALSE(make_network_v4(A::any, 255));
		}

		SECTION("invalid string")
		{
			CHECK_FALSE(make_network_v4(""));
			CHECK_FALSE(make_network_v4("192.168.0.0/"));
			CHECK_FALSE(make_network_v4("192.168.0.0/33"));
			CHECK_FALSE(make_network_v4("192.168.0.0/24 "));
			CHECK_FALSE(make_network_v4("bad/24"));
		}

		SECTION("from_chars")
		{
			auto check_fail = [] (std::string_view s)
			{
				network_v4 n;
				CHECK(n.from_chars(s.data(), s.data() + s.size()).ec == std::errc::invalid_argument);
			};
			check_fail("");
			check_fail("192.168.0.0/");
			check_fail("192.168.0.0/33");
			check_fail("bad/24");
		}
	}

	struct case_t
	{
		std::string text;
		A addr;
		uint8_t prefix_len;
		A mask;
		A bcast;
		bool host = false;
		A inside;
		A outside;
	};

	// clang-format off
	auto expected = GENERATE(values<case_t>({
		{
			.text = "0.0.0.0/0",
			.addr = A::any,
			.prefix_len = 0,
			.mask = A::any,
			.bcast = A::broadcast,
			.inside = B{10, 0, 0, 1},
			.outside = A::any
		},
		{
			.text = "10.0.0.0/8",
			.addr = B{10, 0, 0, 0},
			.prefix_len = 8,
			.mask = B{255, 0, 0, 0},
			.bcast = B{10, 255, 255, 255},
			.inside = B{10, 1, 2, 3},
			.outside = B{11, 0, 0, 0}
		},
		{
			.text = "172.16.0.0/12",
			.addr = B{172, 16, 0, 0},
			.prefix_len = 12,
			.mask = B{255, 240, 0, 0},
			.bcast = B{172, 31, 255, 255},
			.inside = B{172, 20, 0, 1},
			.outside = B{172, 15, 0, 1}
		},
		{
			.text = "192.168.0.0/16",
			.addr = B{192, 168, 0, 0},
			.prefix_len = 16,
			.mask = B{255, 255, 0, 0},
			.bcast = B{192, 168, 255, 255},
			.inside = B{192, 168, 5, 5},
			.outside = B{192, 169, 0, 0}
		},
		{
			.text = "192.168.1.0/24",
			.addr = B{192, 168, 1, 0},
			.prefix_len = 24,
			.mask = B{255, 255, 255, 0},
			.bcast = B{192, 168, 1, 255},
			.inside = B{192, 168, 1, 5},
			.outside = B{192, 168, 2, 0}
		},
		{
			.text = "192.168.1.1/32",
			.addr = B{192, 168, 1, 1},
			.prefix_len = 32,
			.mask = B{255, 255, 255, 255},
			.bcast = B{192, 168, 1, 1},
			.host = true,
			.inside = B{192, 168, 1, 1},
			.outside = B{192, 168, 1, 0}
		},
	}));
	// clang-format on

	CAPTURE(expected.text);

	SECTION("make_network_v4(address_v4, prefix_len)")
	{
		auto n = make_network_v4(expected.addr, expected.prefix_len);
		REQUIRE(n);
		CHECK(n->address() == expected.addr);
		CHECK(n->prefix_length() == expected.prefix_len);
	}

	SECTION("make_network_v4(string_view)")
	{
		auto n = make_network_v4(expected.text);
		REQUIRE(n);
		CHECK(n->address() == expected.addr);
		CHECK(n->prefix_length() == expected.prefix_len);

		if (expected.host)
		{
			auto n2 = make_network_v4(std::format("{}", expected.addr));
			REQUIRE(n2);
			CHECK(*n2 == *n);
		}
	}

	SECTION("canonicalization")
	{
		const auto host_addr = A{expected.bcast.to_uint()};
		auto n = make_network_v4(host_addr, expected.prefix_len);
		REQUIRE(n);
		CHECK(n->address() == expected.addr);
	}

	SECTION("properties")
	{
		auto n = *make_network_v4(expected.addr, expected.prefix_len);
		CHECK(n.netmask() == expected.mask);
		CHECK(n.broadcast() == expected.bcast);
		CHECK(n.is_host() == expected.host);
	}

	SECTION("contains")
	{
		auto n = *make_network_v4(expected.addr, expected.prefix_len);
		CHECK(n.contains(expected.addr));
		CHECK(n.contains(expected.bcast));
		CHECK(n.contains(expected.inside));
		if (expected.prefix_len > 0)
		{
			CHECK_FALSE(n.contains(expected.outside));
		}
	}

	SECTION("hash")
	{
		auto n = *make_network_v4(expected.addr, expected.prefix_len);
		CHECK(n.hash() != 0);
	}

	SECTION("format")
	{
		auto n = *make_network_v4(expected.addr, expected.prefix_len);
		CHECK(std::format("{}", n) == expected.text);
	}

	SECTION("to_chars")
	{
		auto n = *make_network_v4(expected.addr, expected.prefix_len);
		std::array<char, network_v4::max_string_length + 1> buf{};
		auto [p, ec] = n.to_chars(buf.data(), buf.data() + buf.size());
		REQUIRE(ec == std::errc{});
		CHECK(std::string_view{buf.data(), p} == expected.text);
	}

	SECTION("to_chars failure")
	{
		auto n = *make_network_v4(expected.addr, expected.prefix_len);
		const size_t max_size = expected.text.size(), min_size = 0;
		auto buf_size = GENERATE_COPY(range(min_size, max_size));
		std::string buf(buf_size, '\0');
		auto [p, ec] = n.to_chars(buf.data(), buf.data() + buf.size());
		CHECK(ec == std::errc::value_too_large);
		CHECK(p == buf.data() + buf.size());
	}

	SECTION("from_chars")
	{
		network_v4 n;
		auto [p, ec] = n.from_chars(expected.text.data(), expected.text.data() + expected.text.size());
		REQUIRE(ec == std::errc{});
		CHECK(p == expected.text.data() + expected.text.size());
		CHECK(n == *make_network_v4(expected.addr, expected.prefix_len));
	}

	SECTION("from_chars streaming")
	{
		const auto input = expected.text + ",next";
		network_v4 n;
		auto [p, ec] = n.from_chars(input.data(), input.data() + input.size());
		REQUIRE(ec == std::errc{});
		CHECK(*p == ',');
		CHECK(n == *make_network_v4(expected.addr, expected.prefix_len));
	}

	SECTION("from_chars streaming address-only")
	{
		const auto input = std::format("{},next", expected.addr);
		network_v4 n;
		auto [p, ec] = n.from_chars(input.data(), input.data() + input.size());
		REQUIRE(ec == std::errc{});
		CHECK(*p == ',');
		CHECK(n.prefix_length() == network_v4::max_prefix_length);
		CHECK(n.address() == expected.addr);
	}

	SECTION("from_chars failure")
	{
		const auto bad = 'x' + expected.text;
		network_v4 n;
		const auto r = n.from_chars(bad.data(), bad.data() + bad.size());
		REQUIRE(r.ec == std::errc::invalid_argument);
		CHECK(r.ptr == bad.data());
	}
}

// NOLINTEND(readability-magic-numbers)

} // namespace
