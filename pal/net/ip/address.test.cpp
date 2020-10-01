#include <pal/net/ip/address>
#include <pal/net/test>
#include <sstream>


namespace {


TEST_CASE("net/ip/address")
{
	using namespace pal::net::ip;

	SECTION("well-known addresses")
	{
		// V4
		CHECK(address{address_v4::any()}.is_unspecified());
		CHECK(address{address_v4::loopback()}.is_loopback());

		// V4
		CHECK(address{address_v6::any()}.is_unspecified());
		CHECK(address{address_v6::loopback()}.is_loopback());
	}

	SECTION("ctor")
	{
		address a;
		CHECK(a.is_v4());
		CHECK(a.is_unspecified());
	}

	SECTION("ctor(address)")
	{
		SECTION("v4")
		{
			address a = address_v4::loopback();
			address b{a};
			CHECK(b.is_v4());
			CHECK(b.is_loopback());
		}
		SECTION("v6")
		{
			address a = address_v6::loopback();
			address b{a};
			CHECK(b.is_v6());
			CHECK(b.is_loopback());
		}
	}

	SECTION("ctor(address_v4)")
	{
		address a{address_v4::loopback()};
		CHECK(a.is_v4());
		CHECK(a.is_loopback());
	}

	SECTION("ctor(address_v6)")
	{
		address a{address_v6::loopback()};
		CHECK(a.is_v6());
		CHECK(a.is_loopback());
	}

	SECTION("operator=(address_v4)")
	{
		address a;
		a = address_v4::loopback();
		CHECK(a.is_v4());
		CHECK(a.is_loopback());
	}

	SECTION("operator=(address_v6)")
	{
		address a;
		a = address_v6::loopback();
		CHECK(a.is_v6());
		CHECK(a.is_loopback());
	}

	SECTION("cast")
	{
		SECTION("v4")
		{
			address a = address_v4::loopback();
			CHECK(a.as_v4() != nullptr);
			CHECK(a.as_v6() == nullptr);
			CHECK_NOTHROW(a.to_v4());
			CHECK_THROWS_AS(a.to_v6(), std::bad_cast);
		}

		SECTION("v6")
		{
			address a = address_v6::loopback();
			CHECK(a.as_v6() != nullptr);
			CHECK(a.as_v4() == nullptr);
			CHECK_NOTHROW(a.to_v6());
			CHECK_THROWS_AS(a.to_v4(), std::bad_cast);
		}
	}

	SECTION("compare")
	{
		address any_v4 = address_v4::any(),
			any_v6 = address_v6::any(),
			loopback_v4 = address_v4::loopback(),
			loopback_v6 = address_v6::loopback();

		CHECK_FALSE(any_v4 == loopback_v4);
		CHECK(any_v4 != loopback_v4);
		CHECK(any_v4 < loopback_v4);
		CHECK(any_v4 <= loopback_v4);
		CHECK_FALSE(any_v4 > loopback_v4);
		CHECK_FALSE(any_v4 >= loopback_v4);

		CHECK_FALSE(any_v6 == loopback_v6);
		CHECK(any_v6 != loopback_v6);
		CHECK(any_v6 < loopback_v6);
		CHECK(any_v6 <= loopback_v6);
		CHECK_FALSE(any_v6 > loopback_v6);
		CHECK_FALSE(any_v6 >= loopback_v6);

		CHECK_FALSE(any_v4 == any_v6);
		CHECK(any_v4 != any_v6);
		CHECK(any_v4 < any_v6);
		CHECK(any_v4 <= any_v6);
		CHECK_FALSE(any_v4 > any_v6);
		CHECK_FALSE(any_v4 >= any_v6);

		CHECK_FALSE(any_v6 == any_v4);
		CHECK(any_v6 != any_v4);
		CHECK_FALSE(any_v6 < any_v4);
		CHECK_FALSE(any_v6 <= any_v4);
		CHECK(any_v6 > any_v4);
		CHECK(any_v6 >= any_v4);

		CHECK_FALSE(any_v4 == loopback_v6);
		CHECK(any_v4 != loopback_v6);
		CHECK(any_v4 < loopback_v6);
		CHECK(any_v4 <= loopback_v6);
		CHECK_FALSE(any_v4 > loopback_v6);
		CHECK_FALSE(any_v4 >= loopback_v6);

		CHECK_FALSE(any_v6 == loopback_v4);
		CHECK(any_v6 != loopback_v4);
		CHECK_FALSE(any_v6 < loopback_v4);
		CHECK_FALSE(any_v6 <= loopback_v4);
		CHECK(any_v6 > loopback_v4);
		CHECK(any_v6 >= loopback_v4);
	}

	auto [addr, c_str, is_unspecified, is_loopback, is_multicast] = GENERATE(
		table<address, const char *, bool, bool, bool>({
			{ address_v4::any(), "0.0.0.0", true, false, false },
			{ address_v6::any(), "::", true, false, false },
			{ address_v4::loopback(), "127.0.0.1", false, true, false },
			{ address_v6::loopback(), "::1", false, true, false },
			{ address_v4({224,1,2,3}), "224.1.2.3", false, false, true },
			{ address_v6({0xff,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1}), "ff00::1", false, false, true }
		})
	);

	SECTION("load_from / store_to")
	{
		// success
		sockaddr_storage ss;
		addr.store_to(ss);
		address a;
		CHECK(a.try_load_from(ss));
		CHECK(a == addr);
		CHECK_NOTHROW(a.load_from(ss));
		CHECK(a == addr);

		// failure
		ss.ss_family = AF_INET + AF_INET6;
		CHECK_FALSE(a.try_load_from(ss));
		CHECK(a == addr);
		CHECK_THROWS_AS(
			a.load_from(ss),
			pal::net::ip::bad_address_cast
		);
		CHECK(a == addr);
	}

	SECTION("properties")
	{
		CHECK(addr.is_unspecified() == is_unspecified);
		CHECK(addr.is_loopback() == is_loopback);
		CHECK(addr.is_multicast() == is_multicast);
	}

	SECTION("to_string")
	{
		CHECK(addr.to_string() == c_str);
	}

	SECTION("to_chars failure")
	{
		char buf[1];
		auto [p, ec] = addr.to_chars(buf, buf + sizeof(buf));
		CHECK(p == buf + sizeof(buf));
		CHECK(ec == std::errc::value_too_large);
	}

	SECTION("operator<<(std::ostream)")
	{
		std::ostringstream oss;
		oss << addr;
		CHECK(oss.str() == c_str);
	}

	SECTION("hash")
	{
		CHECK(addr.hash() != 0);
	}

	SECTION("make_address(const char *)")
	{
		std::error_code error;
		auto a = make_address(c_str, error);
		CHECK(!error);
		CHECK(a == addr);
		CHECK_NOTHROW(make_address(c_str));
	}

	SECTION("make_address(const char *) failure")
	{
		std::error_code error;
		std::string s = "x";
		s += c_str;
		make_address(s.c_str(), error);
		CHECK(error == std::errc::invalid_argument);

		CHECK_THROWS_AS(make_address(s.c_str()), std::system_error);
	}

	SECTION("make_address(std::string)")
	{
		std::string string = c_str;
		std::error_code error;
		auto a = make_address(string, error);
		CHECK(!error);
		CHECK(a == addr);
		CHECK_NOTHROW(make_address(string));
	}

	SECTION("make_address(std::string) failure")
	{
		std::error_code error;
		std::string string = "x";
		string += c_str;
		make_address(string, error);
		CHECK(error == std::errc::invalid_argument);

		CHECK_THROWS_AS(make_address(string), std::system_error);
	}

	SECTION("make_address(std::string_view)")
	{
		std::string string = c_str;
		std::error_code error;
		auto a = make_address(std::string_view{string}, error);
		CHECK(!error);
		CHECK(a == addr);
		CHECK_NOTHROW(make_address(std::string_view{string}));
	}

	SECTION("make_address(std::string_view) failure")
	{
		std::error_code error;
		std::string string = "xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx";
		string += c_str;
		make_address(std::string_view{string}, error);
		CHECK(error == std::errc::invalid_argument);

		CHECK_THROWS_AS(make_address(std::string_view{string}), std::system_error);
	}
}


} // namespace
