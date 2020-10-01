#include <pal/net/ip/address_v6>
#include <pal/net/test>
#include <sstream>


namespace {


constexpr pal::net::ip::address_v6 long_address{{
	0xff,0xff,0xff,0xff,
	0xff,0xff,0xff,0xff,
	0xff,0xff,0xff,0xff,
	0xff,0xff,0xff,0xff,
}};


TEST_CASE("net/ip/address_v6")
{
	SECTION("well-known addresses")
	{
		CHECK(pal::net::ip::address_v6::any().is_unspecified());
		CHECK(pal::net::ip::address_v6::loopback().is_loopback());
	}

	SECTION("ctor")
	{
		pal::net::ip::address_v6 a;
		CHECK(a.is_unspecified());
	}

	SECTION("ctor(address_v6)")
	{
		auto a = pal::net::ip::address_v6::loopback();
		auto b{a};
		CHECK(a == b);
	}

	SECTION("comparisons")
	{
		auto a = pal::net::ip::address_v6::any();
		auto b = pal::net::ip::address_v6::loopback();
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

	SECTION("to_chars failure")
	{
		char buf[1];
		auto [p, ec] = pal::net::ip::address_v6::any().to_chars(buf, buf + sizeof(buf));
		CHECK(p == buf + sizeof(buf));
		CHECK(ec == std::errc::value_too_large);
	}

	SECTION("to_string failure")
	{
		pal_test::bad_alloc_once x;
		CHECK_THROWS_AS(long_address.to_string(), std::bad_alloc);
	}

	auto
	[
		as_bytes,
		as_cstr,
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
		pal::net::ip::address_v6::bytes_type,
		const char *,
		bool, bool, bool, bool, bool, bool, bool, bool, bool, bool, bool
	>({
		{
			{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15},
			"1:203:405:607:809:a0b:c0d:e0f",
			false, false, false, false, false, false, false, false, false, false, false
		},
		{
			{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
			"::",
			true, false, false, false, false, false, false, false, false, false, false
		},
		{
			{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
			"::1",
			false, true, false, false, false, false, false, false, false, false, false
		},
		{
			{0xfe,0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01},
			"fe80::1",
			false, false, true, false, false, false, false, false, false, false, false
		},
		{
			{0xfe,0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01},
			"fec0::1",
			false, false, false, true, false, false, false, false, false, false, false
		},
		{
			// broken on Windows
			{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0x01,0x02,0x03,0x04},
			"::ffff:1.2.3.4",
			false, false, false, false, true, false, false, false, false, false, false
		},
		{
			{0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0x00,0x00,0x00,0x00},
			"100::ffff:0:0",
			false, false, false, false, false, false, false, false, false, false, false
		},
		{
			{0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0x00,0x00,0x00,0x00},
			"1::ffff:0:0",
			false, false, false, false, false, false, false, false, false, false, false
		},
		{
			{0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0x00,0x00,0x00,0x00},
			"0:100::ffff:0:0",
			false, false, false, false, false, false, false, false, false, false, false
		},
		{
			{0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0x00,0x00,0x00,0x00},
			"0:1::ffff:0:0",
			false, false, false, false, false, false, false, false, false, false, false
		},
		{
			{0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0x00,0x00,0x00,0x00},
			"::100:0:0:ffff:0:0",
			false, false, false, false, false, false, false, false, false, false, false
		},
		{
			{0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0xff,0xff,0x00,0x00,0x00,0x00},
			"::1:0:0:ffff:0:0",
			false, false, false, false, false, false, false, false, false, false, false
		},
		{
			{0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0xff,0xff,0x00,0x00,0x00,0x00},
			"::100:0:ffff:0:0",
			false, false, false, false, false, false, false, false, false, false, false
		},
		{
			{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0xff,0xff,0x00,0x00,0x00,0x00},
			"::1:0:ffff:0:0",
			false, false, false, false, false, false, false, false, false, false, false
		},
		{
			{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0xff,0xff,0x00,0x00,0x00,0x00},
			"::100:ffff:0:0",
			false, false, false, false, false, false, false, false, false, false, false
		},
		{
			{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0xff,0xff,0x00,0x00,0x00,0x00},
			"::1:ffff:0:0",
			false, false, false, false, false, false, false, false, false, false, false
		},
		{
			{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xfe,0x00,0x00,0x00,0x00},
			"::fffe:0:0",
			false, false, false, false, false, false, false, false, false, false, false
		},
		{
			{0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01},
			"ff00::1",
			false, false, false, false, false, true, false, false, false, false, false
		},
		{
			{0xff,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01},
			"ff01::1",
			false, false, false, false, false, true, true, false, false, false, false
		},
		{
			{0xff,0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01},
			"ff02::1",
			false, false, false, false, false, true, false, true, false, false, false
		},
		{
			{0xff,0x05,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01},
			"ff05::1",
			false, false, false, false, false, true, false, false, true, false, false
		},
		{
			{0xff,0x08,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01},
			"ff08::1",
			false, false, false, false, false, true, false, false, false, true, false
		},
		{
			{0xff,0x0e,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01},
			"ff0e::1",
			false, false, false, false, false, true, false, false, false, false, true
		},
	}));

	pal::net::ip::address_v6 address{as_bytes};
	CAPTURE(address);
	CAPTURE(as_cstr);
	CAPTURE(is_unspecified);
	CAPTURE(is_loopback);
	CAPTURE(is_link_local);
	CAPTURE(is_site_local);
	CAPTURE(is_v4_mapped);
	CAPTURE(is_multicast);
	CAPTURE(is_multicast_node_local);
	CAPTURE(is_multicast_link_local);
	CAPTURE(is_multicast_site_local);
	CAPTURE(is_multicast_org_local);
	CAPTURE(is_multicast_global);

	std::error_code error;

	SECTION("ctor(bytes_type)")
	{
		CHECK(address.to_bytes() == as_bytes);
	}

	SECTION("load_from / store_to")
	{
		in6_addr in;
		address.store_to(in);
		pal::net::ip::address_v6 a;
		a.load_from(in);
		CHECK(address == a);
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
		*p = '\0';
		CHECK(std::string{buf, p} == as_cstr);
	}

	SECTION("to_string")
	{
		CHECK(address.to_string() == as_cstr);
	}

	SECTION("operator<<(std::ostream)")
	{
		std::ostringstream oss;
		oss << address;
		CHECK(oss.str() == as_cstr);
	}

	SECTION("make_address_v6(bytes_type)")
	{
		CHECK(address == pal::net::ip::make_address_v6(as_bytes));
	}

	SECTION("make_address_v6(char *)")
	{
		CHECK(address == pal::net::ip::make_address_v6(as_cstr, error));
		CHECK(!error);

		CHECK_NOTHROW(pal::net::ip::make_address_v6(as_cstr));
	}

	SECTION("make_address_v6(char *) failure")
	{
		std::string s = "x";
		s += as_cstr;
		pal::net::ip::make_address_v6(s.c_str(), error);
		CHECK(error == std::errc::invalid_argument);

		CHECK_THROWS_AS(
			pal::net::ip::make_address_v6(s.c_str()),
			std::system_error
		);
	}

	SECTION("make_address_v6(string)")
	{
		std::string as_string = as_cstr;
		CHECK(address == pal::net::ip::make_address_v6(as_string, error));
		CHECK(!error);

		CHECK_NOTHROW(pal::net::ip::make_address_v6(as_string));
	}

	SECTION("make_address_v6(string) failure")
	{
		std::string s = "x";
		s += as_cstr;
		pal::net::ip::make_address_v6(s, error);
		CHECK(error == std::errc::invalid_argument);

		CHECK_THROWS_AS(
			pal::net::ip::make_address_v6(s),
			std::system_error
		);
	}

	SECTION("make_address_v6(string_view)")
	{
		std::string_view view{as_cstr};
		CHECK(address == pal::net::ip::make_address_v6(view, error));
		CHECK(!error);

		CHECK_NOTHROW(pal::net::ip::make_address_v6(view));
	}

	SECTION("make_address_v6(string_view) failure")
	{
		std::string s = "xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx";
		s += as_cstr;
		std::string_view view{s};
		pal::net::ip::make_address_v6(view, error);
		CHECK(error == std::errc::invalid_argument);

		CHECK_THROWS_AS(
			pal::net::ip::make_address_v6(view),
			std::system_error
		);
	}

	SECTION("v4_mapped")
	{
		if (address.is_v4_mapped())
		{
			auto v4 = pal::net::ip::make_address_v4(pal::net::ip::v4_mapped, address, error);
			CHECK(!error);

			auto v6 = pal::net::ip::make_address_v6(pal::net::ip::v4_mapped, v4);
			CHECK(v6 == address);

			CHECK_NOTHROW(
				pal::net::ip::make_address_v4(pal::net::ip::v4_mapped, address)
			);
		}
	}

	SECTION("v4_mapped failure")
	{
		if (!address.is_v4_mapped())
		{
			(void)pal::net::ip::make_address_v4(pal::net::ip::v4_mapped, address, error);
			CHECK(error == std::errc::invalid_argument);

			CHECK_THROWS_AS(
				pal::net::ip::make_address_v4(pal::net::ip::v4_mapped, address),
				pal::net::ip::bad_address_cast
			);
		}
	}
}


} // namespace
