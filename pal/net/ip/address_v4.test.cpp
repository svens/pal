#include <pal/net/ip/address_v4>
#include <pal/net/test>
#include <sstream>


namespace {


constexpr auto to_uint (const pal::net::ip::address_v4::bytes_type &bytes) noexcept
	-> pal::net::ip::address_v4::uint_type
{
	return (bytes[0] << 24) | (bytes[1] << 16) | (bytes[2] << 8) | bytes[3];
}


TEST_CASE("net/ip/address_v4")
{
	SECTION("well-known addresses")
	{
		CHECK(pal::net::ip::address_v4::any().is_unspecified());
		CHECK(pal::net::ip::address_v4::loopback().is_loopback());
		CHECK_FALSE(pal::net::ip::address_v4::broadcast().is_multicast());
	}

	SECTION("ctor")
	{
		pal::net::ip::address_v4 a;
		CHECK(a.to_uint() == 0);
	}

	SECTION("ctor(address_v4)")
	{
		auto a = pal::net::ip::address_v4::loopback;
		auto b{a};
		CHECK(a == b);
	}

	SECTION("comparisons")
	{
		auto a = pal::net::ip::address_v4::any();
		auto b = pal::net::ip::address_v4::broadcast();
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
		auto [p, ec] = pal::net::ip::address_v4::any().to_chars(buf, buf + sizeof(buf));
		CHECK(p == buf + sizeof(buf));
		CHECK(ec == std::errc::value_too_large);
	}

	auto [as_bytes, as_cstr, is_unspecified, is_loopback, is_multicast, is_private] = GENERATE(
		table<pal::net::ip::address_v4::bytes_type, std::string, bool, bool, bool, bool>({
		{ {0,0,0,0},         "0.0.0.0",         true,  false, false, false },
		{ {127,0,0,1},       "127.0.0.1",       false, true,  false, false },
		{ {255,255,255,255}, "255.255.255.255", false, false, false, false },
		{ {224,1,2,3},       "224.1.2.3",       false, false, true,  false },
		{ {10,1,2,3},        "10.1.2.3",        false, false, false, true  },
		{ {172,15,0,1},      "172.15.0.1",      false, false, false, false },
		{ {172,16,0,1},      "172.16.0.1",      false, false, false, true  },
		{ {172,20,0,1},      "172.20.0.1",      false, false, false, true  },
		{ {172,31,255,255},  "172.31.255.255",  false, false, false, true  },
		{ {172,32,0,1},      "172.32.0.1",      false, false, false, false },
		{ {192,168,1,2},     "192.168.1.2",     false, false, false, true  },
		{ {192,169,0,1},     "192.169.0.1",     false, false, false, false },
	}));
	auto as_uint = to_uint(as_bytes);

	pal::net::ip::address_v4 address_from_bytes{as_bytes};
	pal::net::ip::address_v4 address_from_uint{as_uint};

	SECTION("ctor(uint_type)")
	{
		CHECK(address_from_uint.to_bytes() == as_bytes);
	}

	SECTION("ctor(bytes_type)")
	{
		CHECK(address_from_bytes.to_uint() == as_uint);
	}

	SECTION("properties")
	{
		CHECK(address_from_bytes.is_unspecified() == is_unspecified);
		CHECK(address_from_bytes.is_loopback() == is_loopback);
		CHECK(address_from_bytes.is_multicast() == is_multicast);
		CHECK(address_from_bytes.is_private() == is_private);
	}

	SECTION("hash")
	{
		CHECK(address_from_bytes.hash() != 0);
	}

	SECTION("to_string")
	{
		CHECK(address_from_bytes.to_string() == as_cstr);
	}

	SECTION("operator<<(std::ostream)")
	{
		std::ostringstream oss;
		oss << address_from_bytes;
		CHECK(oss.str() == as_cstr);
	}

	SECTION("make_address_v4(bytes_type)")
	{
		CHECK(address_from_bytes == pal::net::ip::make_address_v4(as_bytes));
	}

	SECTION("make_address_v4(uint_type)")
	{
		CHECK(address_from_bytes == pal::net::ip::make_address_v4(as_uint));
	}

	SECTION("make_address_v4(char *)")
	{
		std::error_code error;
		CHECK(address_from_bytes == pal::net::ip::make_address_v4(as_cstr, error));
		CHECK(!error);

		CHECK_NOTHROW(pal::net::ip::make_address_v4(as_cstr));
	}

	SECTION("make_address_v4(char *) failure")
	{
		std::error_code error;
		std::string s = "x";
		s += as_cstr;
		pal::net::ip::make_address_v4(s.c_str(), error);
		CHECK(error == std::errc::invalid_argument);

		CHECK_THROWS_AS(
			pal::net::ip::make_address_v4(s.c_str()),
			std::system_error
		);
	}

	SECTION("make_address_v4(string)")
	{
		std::string as_string = as_cstr;
		std::error_code error;
		CHECK(address_from_bytes == pal::net::ip::make_address_v4(as_string, error));
		CHECK(!error);

		CHECK_NOTHROW(pal::net::ip::make_address_v4(as_string));
	}

	SECTION("make_address_v4(string) failure")
	{
		std::error_code error;
		std::string s = "x";
		s += as_cstr;
		pal::net::ip::make_address_v4(s, error);
		CHECK(error == std::errc::invalid_argument);

		CHECK_THROWS_AS(
			pal::net::ip::make_address_v4(s),
			std::system_error
		);
	}

	SECTION("make_address_v4(string_view)")
	{
		std::string_view view{as_cstr};
		std::error_code error;
		CHECK(address_from_bytes == pal::net::ip::make_address_v4(view, error));
		CHECK(!error);

		CHECK_NOTHROW(pal::net::ip::make_address_v4(view));
	}

	SECTION("make_address_v4(string_view) failure")
	{
		std::error_code error;
		std::string s = "x";
		s += as_cstr;
		std::string_view view{s};
		pal::net::ip::make_address_v4(view, error);
		CHECK(error == std::errc::invalid_argument);

		CHECK_THROWS_AS(
			pal::net::ip::make_address_v4(view),
			std::system_error
		);
	}
}


} // namespace
