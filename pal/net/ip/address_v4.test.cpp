#include <pal/net/ip/address_v4>
#include <pal/net/test>
#include <sstream>


namespace {


uint32_t to_host_uint (const char *src)
{
	in_addr dest{};
	REQUIRE(inet_pton(AF_INET, src, &dest) == 1);
	return pal::ntoh(static_cast<uint32_t>(dest.s_addr));
}


TEST_CASE("net/ip/address_v4")
{
	using namespace pal::net::ip;
	using ip4 = address_v4::bytes_type;

	SECTION("constexpr")
	{
		constexpr address_v4 a;
		constexpr address_v4 b{ip4{1,2,3,4}};
		constexpr address_v4 c{b};
		constexpr address_v4 d{c.to_uint()};
		constexpr address_v4 e{d.to_bytes()};
		constexpr auto f = e.is_unspecified();
		constexpr auto g = e.is_loopback();
		constexpr auto h = e.is_multicast();
		constexpr auto i = e.is_private();
		constexpr auto j = e.compare(a);
		constexpr auto k = e.hash();
		constexpr auto l = address_v4::any();
		constexpr auto m = address_v4::loopback();
		constexpr auto n = address_v4::broadcast();
		pal_test::unused(a, b, c, d, e, f, g, h, i, j, k, l, m, n);
	}

	SECTION("well-known addresses")
	{
		CHECK(address_v4::any().is_unspecified());
		CHECK(address_v4::loopback().is_loopback());
		CHECK_FALSE(address_v4::broadcast().is_multicast());
	}

	SECTION("ctor")
	{
		address_v4 a;
		CHECK(a.to_uint() == 0);
	}

	SECTION("ctor(address_v4)")
	{
		auto a = address_v4::loopback();
		auto b{a};
		CHECK(a == b);
	}

	SECTION("comparisons")
	{
		auto a = address_v4::any();
		auto b = address_v4::broadcast();
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

	using ip4 = address_v4::bytes_type;
	auto [bytes, c_str, is_unspecified, is_loopback, is_multicast, is_private] = GENERATE(
		table<ip4, const char *, bool, bool, bool, bool>({
		{ ip4{0,0,0,0},         "0.0.0.0",         true,  false, false, false },
		{ ip4{0,1,0,0},         "0.1.0.0",         false, false, false, false },
		{ ip4{0,0,1,0},         "0.0.1.0",         false, false, false, false },
		{ ip4{0,0,0,1},         "0.0.0.1",         false, false, false, false },
		{ ip4{127,0,0,1},       "127.0.0.1",       false, true,  false, false },
		{ ip4{255,255,255,255}, "255.255.255.255", false, false, false, false },
		{ ip4{224,1,2,3},       "224.1.2.3",       false, false, true,  false },
		{ ip4{10,1,2,3},        "10.1.2.3",        false, false, false, true  },
		{ ip4{172,15,0,1},      "172.15.0.1",      false, false, false, false },
		{ ip4{172,16,0,1},      "172.16.0.1",      false, false, false, true  },
		{ ip4{172,20,0,1},      "172.20.0.1",      false, false, false, true  },
		{ ip4{172,31,255,255},  "172.31.255.255",  false, false, false, true  },
		{ ip4{172,32,0,1},      "172.32.0.1",      false, false, false, false },
		{ ip4{192,168,1,2},     "192.168.1.2",     false, false, false, true  },
		{ ip4{192,169,0,1},     "192.169.0.1",     false, false, false, false },
	}));
	CAPTURE(c_str);

	address_v4 address{bytes};
	auto as_uint = to_host_uint(c_str);

	SECTION("ctor(uint_type)")
	{
		CHECK(address_v4(as_uint) == address);
	}

	SECTION("to_bytes")
	{
		CHECK(address.to_bytes() == bytes);
	}

	SECTION("to_uint")
	{
		CHECK(address.to_uint() == as_uint);
	}

	SECTION("properties")
	{
		CHECK(address.is_unspecified() == is_unspecified);
		CHECK(address.is_loopback() == is_loopback);
		CHECK(address.is_multicast() == is_multicast);
		CHECK(address.is_private() == is_private);
	}

	SECTION("hash")
	{
		CHECK(address.hash() != 0);
	}

	SECTION("to_chars")
	{
		char buf[INET_ADDRSTRLEN];
		auto [p, ec] = address.to_chars(buf, buf + sizeof(buf));
		CHECK(ec == std::errc{});
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

	SECTION("make_address_v4(bytes_type)")
	{
		CHECK(address == make_address_v4(bytes));
	}

	SECTION("make_address_v4(uint_type)")
	{
		CHECK(address == make_address_v4(as_uint));
	}

	SECTION("make_address_v4(char *)")
	{
		std::error_code error;
		CHECK(address == make_address_v4(c_str, error));
		CHECK(!error);

		CHECK_NOTHROW(make_address_v4(c_str));
	}

	SECTION("make_address_v4(char *) failure")
	{
		std::error_code error;
		std::string s = "x";
		s += c_str;
		make_address_v4(s.c_str(), error);
		CHECK(error == std::errc::invalid_argument);

		CHECK_THROWS_AS(
			make_address_v4(s.c_str()),
			std::system_error
		);
	}

	SECTION("make_address_v4(string)")
	{
		std::string as_string = c_str;
		std::error_code error;
		CHECK(address == make_address_v4(as_string, error));
		CHECK(!error);

		CHECK_NOTHROW(make_address_v4(as_string));
	}

	SECTION("make_address_v4(string) failure")
	{
		std::error_code error;
		std::string s = "x";
		s += c_str;
		make_address_v4(s, error);
		CHECK(error == std::errc::invalid_argument);

		CHECK_THROWS_AS(
			make_address_v4(s),
			std::system_error
		);
	}

	SECTION("make_address_v4(string_view)")
	{
		std::string_view view{c_str};
		std::error_code error;
		CHECK(address == make_address_v4(view, error));
		CHECK(!error);

		CHECK_NOTHROW(make_address_v4(view));
	}

	SECTION("make_address_v4(string_view) failure")
	{
		std::error_code error;
		std::string s = "x";
		s += c_str;
		std::string_view view{s};
		make_address_v4(view, error);
		CHECK(error == std::errc::invalid_argument);

		CHECK_THROWS_AS(
			make_address_v4(view),
			std::system_error
		);
	}
}


} // namespace
