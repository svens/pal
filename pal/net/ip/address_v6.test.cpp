#include <pal/net/ip/address_v6>
#include <pal/net/test>
#include <sstream>


namespace {


TEST_CASE("net/ip/address_v6")
{
	using namespace pal::net::ip;
	using ip6 = address_v6::bytes_type;

	SECTION("constexpr")
	{
		constexpr address_v6 a;
		constexpr address_v6 b{ip6{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15}};
		constexpr address_v6 c{b};
		constexpr address_v6 d{c.to_bytes()};
		constexpr auto e = d.is_unspecified();
		constexpr auto f = d.is_loopback();
		constexpr auto g = d.is_multicast();
		constexpr auto h = d.compare(a);
		constexpr auto i = d.hash();
		constexpr auto j = address_v6::any();
		constexpr auto k = address_v6::loopback();
		pal_test::unused(a, b, c, d, e, f, g, h, i, j, k);
	}

	SECTION("well-known addresses")
	{
		CHECK(address_v6::any().is_unspecified());
		CHECK(address_v6::loopback().is_loopback());
	}

	SECTION("ctor")
	{
		address_v6 a;
		CHECK(a.is_unspecified());
	}

	SECTION("ctor(address_v6)")
	{
		auto a = address_v6::loopback();
		auto b{a};
		CHECK(a == b);
	}

	SECTION("comparisons")
	{
		auto a = address_v6::any();
		auto b = address_v6::loopback();
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

	auto
	[
		bytes,
		c_str,
		is_unspecified,
		is_loopback,
		is_link_local,
		is_site_local,
		is_v4_mapped,
		is_multicast,
		is_multicast_node_local,
		is_multicast_link_local,
		is_multicast_site_local,
		is_multicast_org_local,
		is_multicast_global
	] =
	GENERATE(table<
		ip6,
		const char *,
		bool, bool, bool, bool, bool, bool, bool, bool, bool, bool, bool
	>({
		{
			ip6{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15},
			"1:203:405:607:809:a0b:c0d:e0f",
			false, false, false, false, false, false, false, false, false, false, false
		},
		{
			ip6{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
			"::",
			true, false, false, false, false, false, false, false, false, false, false
		},
		{
			ip6{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
			"::1",
			false, true, false, false, false, false, false, false, false, false, false
		},
		{
			ip6{0xfe,0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01},
			"fe80::1",
			false, false, true, false, false, false, false, false, false, false, false
		},
		{
			ip6{0xfe,0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01},
			"fec0::1",
			false, false, false, true, false, false, false, false, false, false, false
		},
		{
			ip6{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0x01,0x02,0x03,0x04},
			"::ffff:1.2.3.4",
			false, false, false, false, true, false, false, false, false, false, false
		},
		{
			ip6{0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0x00,0x00,0x00,0x00},
			"100::ffff:0:0",
			false, false, false, false, false, false, false, false, false, false, false
		},
		{
			ip6{0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0x00,0x00,0x00,0x00},
			"1::ffff:0:0",
			false, false, false, false, false, false, false, false, false, false, false
		},
		{
			ip6{0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0x00,0x00,0x00,0x00},
			"0:100::ffff:0:0",
			false, false, false, false, false, false, false, false, false, false, false
		},
		{
			ip6{0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0x00,0x00,0x00,0x00},
			"0:1::ffff:0:0",
			false, false, false, false, false, false, false, false, false, false, false
		},
		{
			ip6{0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0x00,0x00,0x00,0x00},
			"::100:0:0:ffff:0:0",
			false, false, false, false, false, false, false, false, false, false, false
		},
		{
			ip6{0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0xff,0xff,0x00,0x00,0x00,0x00},
			"::1:0:0:ffff:0:0",
			false, false, false, false, false, false, false, false, false, false, false
		},
		{
			ip6{0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0xff,0xff,0x00,0x00,0x00,0x00},
			"::100:0:ffff:0:0",
			false, false, false, false, false, false, false, false, false, false, false
		},
		{
			ip6{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0xff,0xff,0x00,0x00,0x00,0x00},
			"::1:0:ffff:0:0",
			false, false, false, false, false, false, false, false, false, false, false
		},
		{
			ip6{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0xff,0xff,0x00,0x00,0x00,0x00},
			"::100:ffff:0:0",
			false, false, false, false, false, false, false, false, false, false, false
		},
		{
			ip6{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0xff,0xff,0x00,0x00,0x00,0x00},
			"::1:ffff:0:0",
			false, false, false, false, false, false, false, false, false, false, false
		},
		{
			ip6{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xfe,0x00,0x00,0x00,0x00},
			"::fffe:0:0",
			false, false, false, false, false, false, false, false, false, false, false
		},
		{
			ip6{0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01},
			"ff00::1",
			false, false, false, false, false, true, false, false, false, false, false
		},
		{
			ip6{0xff,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01},
			"ff01::1",
			false, false, false, false, false, true, true, false, false, false, false
		},
		{
			ip6{0xff,0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01},
			"ff02::1",
			false, false, false, false, false, true, false, true, false, false, false
		},
		{
			ip6{0xff,0x05,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01},
			"ff05::1",
			false, false, false, false, false, true, false, false, true, false, false
		},
		{
			ip6{0xff,0x08,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01},
			"ff08::1",
			false, false, false, false, false, true, false, false, false, true, false
		},
		{
			ip6{0xff,0x0e,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01},
			"ff0e::1",
			false, false, false, false, false, true, false, false, false, false, true
		},
	}));

	address_v6 address{bytes};
	CAPTURE(address);

	std::error_code error;

	SECTION("to_bytes")
	{
		CHECK(address.to_bytes() == bytes);
	}

	SECTION("properties")
	{
		CHECK(address.is_unspecified() == is_unspecified);
		CHECK(address.is_loopback() == is_loopback);
		CHECK(address.is_link_local() == is_link_local);
		CHECK(address.is_site_local() == is_site_local);
		CHECK(address.is_v4_mapped() == is_v4_mapped);
		CHECK(address.is_multicast() == is_multicast);
		CHECK(address.is_multicast_node_local() == is_multicast_node_local);
		CHECK(address.is_multicast_link_local() == is_multicast_link_local);
		CHECK(address.is_multicast_site_local() == is_multicast_site_local);
		CHECK(address.is_multicast_org_local() == is_multicast_org_local);
		CHECK(address.is_multicast_global() == is_multicast_global);
	}

	SECTION("hash")
	{
		CHECK(address.hash() != 0);
	}

	SECTION("to_chars")
	{
		char buf[INET6_ADDRSTRLEN];
		auto [p, ec] = address.to_chars(buf, buf + sizeof(buf));
		REQUIRE(ec == std::errc{});
		CHECK(std::string(buf, p) == c_str);
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
		CHECK(address.to_string() == c_str);
	}

	SECTION("operator<<(std::ostream)")
	{
		std::ostringstream oss;
		oss << address;
		CHECK(oss.str() == c_str);
	}

	SECTION("make_address_v6(bytes_type)")
	{
		CHECK(address == make_address_v6(bytes));
	}

	SECTION("make_address_v6(char *)")
	{
		CHECK(address == make_address_v6(c_str, error));
		CHECK(!error);

		CHECK_NOTHROW(make_address_v6(c_str));
	}

	SECTION("make_address_v6(char *) failure")
	{
		std::string s = "x";
		s += c_str;
		make_address_v6(s.c_str(), error);
		CHECK(error == std::errc::invalid_argument);

		CHECK_THROWS_AS(
			make_address_v6(s.c_str()),
			std::system_error
		);
	}

	SECTION("make_address_v6(string)")
	{
		std::string as_string = c_str;
		CHECK(address == make_address_v6(as_string, error));
		CHECK(!error);

		CHECK_NOTHROW(make_address_v6(as_string));
	}

	SECTION("make_address_v6(string) failure")
	{
		std::string s = "x";
		s += c_str;
		make_address_v6(s, error);
		CHECK(error == std::errc::invalid_argument);

		CHECK_THROWS_AS(
			make_address_v6(s),
			std::system_error
		);
	}

	SECTION("make_address_v6(string_view)")
	{
		std::string_view view{c_str};
		CHECK(address == make_address_v6(view, error));
		CHECK(!error);

		CHECK_NOTHROW(make_address_v6(view));
	}

	SECTION("make_address_v6(string_view) failure")
	{
		std::string s = "xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx";
		s += c_str;
		std::string_view view{s};
		make_address_v6(view, error);
		CHECK(error == std::errc::invalid_argument);

		CHECK_THROWS_AS(
			make_address_v6(view),
			std::system_error
		);
	}

	SECTION("v4_mapped")
	{
		if (address.is_v4_mapped())
		{
			auto v4 = make_address_v4(v4_mapped, address, error);
			CHECK(!error);

			auto v6 = make_address_v6(v4_mapped, v4);
			CHECK(v6 == address);

			CHECK_NOTHROW(
				make_address_v4(v4_mapped, address)
			);
		}
	}

	SECTION("v4_mapped failure")
	{
		if (!address.is_v4_mapped())
		{
			(void)make_address_v4(v4_mapped, address, error);
			CHECK(error == std::errc::invalid_argument);

			CHECK_THROWS_AS(
				make_address_v4(v4_mapped, address),
				bad_address_cast
			);
		}
	}
}


} // namespace
