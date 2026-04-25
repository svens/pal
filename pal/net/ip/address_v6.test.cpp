#include <pal/net/ip/address_v6.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <cctype>

namespace
{

// NOLINTBEGIN(readability-magic-numbers)

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
		static_assert(a == A::any);
		static_assert(a.is_unspecified());
		static_assert(!a.is_loopback());
		static_assert(!a.is_link_local());
		static_assert(!a.is_site_local());
		static_assert(!a.is_multicast());
		static_assert(a.scope_id() == 0);

		constexpr A b{B{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}};
		static_assert(!b.is_loopback());
		static_assert(!b.is_unspecified());
		static_assert(!b.is_v4_mapped());

		constexpr A c{b.to_bytes()};
		static_assert(b == c);
		static_assert(c != a);
		static_assert(c > a);
		static_assert(c >= a);
		static_assert(!(c < a));
		static_assert(!(c <= a));

		static_assert(A::any.is_unspecified());
		static_assert(A::loopback.is_loopback());

		static_assert(a.hash() != 0);
		static_assert(b.hash() == c.hash());

		// scope_id
		constexpr A d{B{0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}, 5};
		static_assert(d.scope_id() == 5);
		static_assert(d.is_link_local());
		static_assert(!d.is_site_local());
		static_assert(d != b);

		// v4_mapped
		constexpr A v4m{B{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 1, 2, 3, 4}};
		static_assert(v4m.is_v4_mapped());
		static_assert(!A::any.is_v4_mapped());

		// multicast
		constexpr A mc{B{0xff, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}};
		static_assert(mc.is_multicast());
		static_assert(mc.is_multicast_link_local());
		static_assert(!mc.is_multicast_node_local());
		static_assert(!mc.is_multicast_global());
	}

	SECTION("ctor")
	{
		const A a;
		CHECK(a.is_unspecified());
		CHECK(a.scope_id() == 0);
	}

	SECTION("copy ctor")
	{
		const A a;
		auto a1 = a;
		CHECK(a1 == a);
	}

	SECTION("compare")
	{
		const auto a = A::any;
		const auto b = A::loopback;
		const auto c = a;

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

	SECTION("scope_id")
	{
		const B link_local_bytes{0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
		constexpr A::scope_id_type test_scope = 42;

		SECTION("default is zero")
		{
			CHECK(A{link_local_bytes}.scope_id() == 0);
		}

		SECTION("construction with scope_id")
		{
			const A a{link_local_bytes, test_scope};
			CHECK(a.scope_id() == test_scope);
			CHECK(a.to_bytes() == link_local_bytes);
		}

		SECTION("equality includes scope_id")
		{
			const A a1{link_local_bytes, 0};
			const A a2{link_local_bytes, 1};
			CHECK(a1 != a2);
			CHECK(a1 < a2);
		}

		SECTION("hash includes scope_id")
		{
			const A a1{link_local_bytes, 0};
			const A a2{link_local_bytes, 1};
			CHECK(a1.hash() != a2.hash());
		}

		SECTION("to_chars without scope_id")
		{
			const A a{link_local_bytes};
			std::array<char, A::max_string_length + 1> buf{};
			auto [end, ec] = a.to_chars(buf.data(), buf.data() + buf.size());
			REQUIRE(ec == std::errc{});
			const std::string_view sv{buf.data(), static_cast<size_t>(end - buf.data())};
			CHECK(!sv.contains('%'));
		}

		SECTION("to_chars with scope_id")
		{
			const A a{link_local_bytes, test_scope};
			std::array<char, A::max_string_length + 1> buf{};
			auto [end, ec] = a.to_chars(buf.data(), buf.data() + buf.size());
			REQUIRE(ec == std::errc{});
			const std::string_view sv{buf.data(), static_cast<size_t>(end - buf.data())};
			CHECK(sv.ends_with("%42"));
		}

		SECTION("from_chars with scope_id")
		{
			const std::string s = "fe80::1%42";
			A a;
			auto [p, ec] = a.from_chars(s.data(), s.data() + s.size());
			REQUIRE(ec == std::errc{});
			CHECK(a.scope_id() == test_scope);
			CHECK(p == s.data() + s.size());
		}

		SECTION("to_chars scope_id buffer too small")
		{
			// ntop succeeds (needs 8 bytes for "fe80::1\0") but no room for %42
			const A a{link_local_bytes, test_scope};
			const size_t min_size = 8, max_size = 9;
			auto buf_size = GENERATE_COPY(range(min_size, max_size + 1));
			std::string buf(buf_size, '\0');
			auto [p, ec] = a.to_chars(buf.data(), buf.data() + buf.size());
			CHECK(ec == std::errc::value_too_large);
		}

		SECTION("from_chars scope_id streaming")
		{
			const std::string s = "fe80::1%42,next";
			A a;
			auto [p, ec] = a.from_chars(s.data(), s.data() + s.size());
			REQUIRE(ec == std::errc{});
			CHECK(a.scope_id() == test_scope);
			CHECK(*p == ',');
		}

		SECTION("round-trip with scope_id")
		{
			const A a{link_local_bytes, test_scope};
			std::array<char, A::max_string_length + 1> buf{};
			auto [end, ec1] = a.to_chars(buf.data(), buf.data() + buf.size());
			REQUIRE(ec1 == std::errc{});

			A b;
			auto [p, ec2] = b.from_chars(buf.data(), end);
			REQUIRE(ec2 == std::errc{});
			CHECK(b == a);
		}

		SECTION("from_chars failures")
		{
			auto check_fail = [] (std::string_view s)
			{
				A a;
				const auto r = a.from_chars(s.data(), s.data() + s.size());
				CHECK(r.ec != std::errc{});
			};
			check_fail("fe80::1%");		   // empty scope_id
			check_fail("fe80::1%xyz");	   // non-digit scope_id
			check_fail("fe80::1%99999999999"); // overflow
		}
	}

	struct case_t
	{
		std::string view;
		B bytes;
		bool is_unspecified = false;
		bool is_loopback = false;
		bool is_v4_mapped = false;
		bool is_link_local = false;
		bool is_site_local = false;
		bool is_multicast = false;
	};

	// clang-format off
	auto [view, bytes, is_unspecified, is_loopback, is_v4_mapped, is_link_local, is_site_local, is_multicast] = GENERATE(values<case_t>({
		{
			.view = "::",
			.bytes = A::any.to_bytes(),
			.is_unspecified = true
		},
		{
			.view = "::1",
			.bytes = A::loopback.to_bytes(),
			.is_loopback = true
		},
		{
			.view = "::ffff:1.2.3.4",
			.bytes = B{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 0x01, 0x02, 0x03, 0x04},
			.is_v4_mapped = true
		},
		{
			.view = "1:203:405:607:809:a0b:c0d:e0f",
			.bytes = B{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}
		},

		// for branch coverage
		{
			.view = "fe80::1",
			.bytes = B{0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01},
			.is_link_local = true
		},
		{
			.view = "fec0::1",
			.bytes = B{0xfe, 0xc0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01},
			.is_site_local = true
		},
		{
			.view = "100::ffff:0:0",
			.bytes = B{0x01, 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 0, 0, 0, 0}
		},
		{
			.view = "1::ffff:0:0",
			.bytes = B{0x00, 0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 0, 0, 0, 0}
		},
		{
			.view = "0:100::ffff:0:0",
			.bytes = B{0, 0, 0x01, 0x00, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 0, 0, 0, 0}
		},
		{
			.view = "0:1::ffff:0:0",
			.bytes = B{0, 0, 0x00, 0x01, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 0, 0, 0, 0}
		},
		{
			.view = "::100:0:0:ffff:0:0",
			.bytes = B{0, 0, 0, 0, 0x01, 0x00, 0, 0, 0, 0, 0xff, 0xff, 0, 0, 0, 0}
		},
		{
			.view = "::1:0:0:ffff:0:0",
			.bytes = B{0, 0, 0, 0, 0x00, 0x01, 0, 0, 0, 0, 0xff, 0xff, 0, 0, 0, 0}
		},
		{
			.view = "::100:0:ffff:0:0",
			.bytes = B{0, 0, 0, 0, 0, 0, 0x01, 0x00, 0, 0, 0xff, 0xff, 0, 0, 0, 0}
		},
		{
			.view = "::1:0:ffff:0:0",
			.bytes = B{0, 0, 0, 0, 0, 0, 0x00, 0x01, 0, 0, 0xff, 0xff, 0, 0, 0, 0}
		},
		{
			.view = "::100:ffff:0:0",
			.bytes = B{0, 0, 0, 0, 0, 0, 0, 0, 0x01, 0x00, 0xff, 0xff, 0, 0, 0, 0}
		},
		{
			.view = "::1:ffff:0:0",
			.bytes = B{0, 0, 0, 0, 0, 0, 0, 0, 0x00, 0x01, 0xff, 0xff, 0, 0, 0, 0}
		},
		{
			.view = "::fffe:0:0",
			.bytes = B{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xfe, 0, 0, 0, 0}
		},

		// multicast (ff00::/8) — scope nibble in bytes[1] & 0x0f
		{
			.view = "ff00::1",
			.bytes = B{0xff, 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01},
			.is_multicast = true
		},
		{
			.view = "ff01::1",
			.bytes = B{0xff, 0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01},
			.is_multicast = true
		},
		{
			.view = "ff02::1",
			.bytes = B{0xff, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01},
			.is_multicast = true
		},
		{
			.view = "ff05::1",
			.bytes = B{0xff, 0x05, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01},
			.is_multicast = true
		},
		{
			.view = "ff08::1",
			.bytes = B{0xff, 0x08, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01},
			.is_multicast = true
		},
		{
			.view = "ff0e::1",
			.bytes = B{0xff, 0x0e, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01},
			.is_multicast = true
		},
		{
			.view = "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff",
			.bytes = B{0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff},
			.is_multicast = true
		},
	}));
	// clang-format on

	CAPTURE(view);

	const A a{bytes};
	CHECK(a.to_bytes() == bytes);

	SECTION("properties")
	{
		CHECK(a.is_unspecified() == is_unspecified);
		CHECK(a.is_loopback() == is_loopback);
		CHECK(a.is_v4_mapped() == is_v4_mapped);
		CHECK(a.is_link_local() == is_link_local);
		CHECK(a.is_site_local() == is_site_local);
		CHECK(a.is_multicast() == is_multicast);
	}

	SECTION("multicast scope")
	{
		if (a.is_multicast())
		{
			CHECK(a.is_multicast_node_local() == ((bytes[1] & 0x0f) == 0x01));
			CHECK(a.is_multicast_link_local() == ((bytes[1] & 0x0f) == 0x02));
			CHECK(a.is_multicast_site_local() == ((bytes[1] & 0x0f) == 0x05));
			CHECK(a.is_multicast_org_local() == ((bytes[1] & 0x0f) == 0x08));
			CHECK(a.is_multicast_global() == ((bytes[1] & 0x0f) == 0x0e));
		}
		else
		{
			CHECK_FALSE(a.is_multicast_node_local());
			CHECK_FALSE(a.is_multicast_link_local());
			CHECK_FALSE(a.is_multicast_site_local());
			CHECK_FALSE(a.is_multicast_org_local());
			CHECK_FALSE(a.is_multicast_global());
		}
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

	SECTION("from_chars uppercase")
	{
		auto upper = view;
		for (auto &c: upper)
		{
			c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
		}
		A b;
		auto [p, ec] = b.from_chars(upper.data(), upper.data() + upper.size());
		REQUIRE(ec == std::errc{});
		CHECK(b == a);
		CHECK(p == upper.data() + upper.size());
	}

	SECTION("from_chars streaming")
	{
		const auto input = view + ",next";
		A b;
		auto [p, ec] = b.from_chars(input.data(), input.data() + input.size());
		REQUIRE(ec == std::errc{});
		CHECK(*p == ',');
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
		const std::string bad = ":::";
		A b;
		const auto r = b.from_chars(bad.data(), bad.data() + bad.size());
		CHECK(r.ec == std::errc::invalid_argument);
		CHECK(r.ptr == bad.data() + bad.size());
	}

	SECTION("make_address_v6(bytes_type)")
	{
		CHECK(a == make_address_v6(bytes));
	}

	SECTION("make_address_v6(bytes_type, scope_id)")
	{
		const auto b = make_address_v6(bytes, A::scope_id_type{42});
		CHECK(b.to_bytes() == bytes);
		CHECK(b.scope_id() == 42);
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
		CHECK(v.error() == std::make_error_code(std::errc::invalid_argument));
	}

	SECTION("v4_mapped")
	{
		auto v4 = make_address_v4(v4_mapped, a);
		if (a.is_v4_mapped())
		{
			REQUIRE(v4);
			const auto v6 = make_address_v6(v4_mapped, *v4);
			CHECK(v6 == a);
		}
		else
		{
			REQUIRE_FALSE(v4);
			CHECK(v4.error() == std::make_error_code(std::errc::invalid_argument));
		}
	}

	SECTION("v4_mapped with scope_id")
	{
		const A scoped{B{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 1, 2, 3, 4}, 5};
		const auto v4 = make_address_v4(v4_mapped, scoped);
		REQUIRE(v4);
		CHECK(v4->to_bytes() == pal::net::ip::address_v4::bytes_type{1, 2, 3, 4});
	}

	SECTION("format")
	{
		CHECK(std::format("{}", a) == view);
	}

	SECTION("masked")
	{
		auto masked_bytes = bytes;
		std::fill(masked_bytes.begin() + 8, masked_bytes.end(), uint8_t{});
		const A masked_addr{masked_bytes};
		std::array<char, A::max_string_length + 1> expected_buf{};
		auto [expected_end, ec1] =
			masked_addr.to_chars(expected_buf.data(), expected_buf.data() + expected_buf.size());
		REQUIRE(ec1 == std::errc{});
		const std::string expected{expected_buf.data(), expected_end};

		CHECK(std::format("{}", pal::masked{a}) == expected);

		A parsed;
		auto [p, ec2] = parsed.from_chars(expected.data(), expected.data() + expected.size());
		CHECK(ec2 == std::errc{});
	}
}

// NOLINTEND(readability-magic-numbers)

} // namespace
