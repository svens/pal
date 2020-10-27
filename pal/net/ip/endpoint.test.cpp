#include <pal/net/ip/tcp>
#include <pal/net/ip/udp>
#include <pal/net/test>
#include <cstring>
#include <sstream>


namespace {


using namespace pal_test;
using namespace pal::net::ip;


TEMPLATE_TEST_CASE("net/ip/endpoint", "", tcp, udp)
{
	constexpr port_type port = 60000;
	using endpoint_type = typename TestType::endpoint;

	/* TODO: use family attribute in endpoint to make it truly constexpr
	SECTION("constexpr")
	{
		constexpr endpoint_type a;
		constexpr endpoint_type b(protocol_type::v4(), port);
		constexpr endpoint_type c(address_v4::any(), port);
		constexpr endpoint_type d(address_v6::any(), port);

		constexpr auto e = c.protocol();
		constexpr auto f = d.protocol();

		constexpr auto g = c.address();
		constexpr auto h = d.address();

		constexpr auto i = c.port();
		constexpr auto j = d.port();

		constexpr auto k = c.size();
		constexpr auto l = d.size();

		constexpr auto m = c.capacity();
		constexpr auto n = d.capacity();

		constexpr auto o = c.compare(d);
		constexpr auto p = d.compare(c);

		constexpr auto q = c.hash();
		constexpr auto r = d.hash();

		pal_test::unused(a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p, q, r);
	}
	*/

	SECTION("ctor")
	{
		endpoint_type endpoint;
		CHECK(endpoint.protocol().family() == AF_INET);
	}

	SECTION("compare")
	{
		auto [aa, ba] = GENERATE(
			table<pal::net::ip::address, pal::net::ip::address>({
			{ address_v4::any(), address_v4::loopback() },
			{ address_v6::any(), address_v6::loopback() },
			{ address_v4::any(), address_v6::loopback() },
		}));

		endpoint_type a(aa, port), b(ba, port);
		auto c = a;

		CHECK(a != b);
		CHECK(a == c);

		CHECK(a < b);
		CHECK(a <= b);
		CHECK(a <= c);

		CHECK_FALSE(a > b);
		CHECK_FALSE(a >= b);
		CHECK(a >= c);
	}

	using ip4 = address_v4::bytes_type;
	using ip6 = address_v6::bytes_type;

	auto [protocol, address, c_str] = GENERATE(
		table<TestType, pal::net::ip::address, const char *>({
		{
			TestType::v4(),
			address_v4::loopback(),
			"127.0.0.1:60000"
		},
		{
			TestType::v4(),
			address_v4{ip4{255,255,255,255}},
			"255.255.255.255:60000"
		},
		{
			TestType::v6(),
			address_v6::loopback(),
			"[::1]:60000"
		},
		{
			TestType::v6(),
			address_v6{ip6{255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255}},
			"[ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff]:60000"
		},
	}));
	CAPTURE(c_str);

	SECTION("ctor(protocol, port)")
	{
		endpoint_type endpoint(protocol, port);
		CHECK(endpoint.protocol() == protocol);
		CHECK(endpoint.port() == port);
	}

	SECTION("ctor(address, port)")
	{
		endpoint_type endpoint(address, port);
		CHECK(endpoint.address() == address);
		CHECK(endpoint.protocol() == protocol);
		CHECK(endpoint.port() == port);
	}

	SECTION("protocol")
	{
		endpoint_type endpoint(protocol, port);
		CHECK(endpoint.protocol() == protocol);
	}

	SECTION("address")
	{
		endpoint_type endpoint;
		CHECK(endpoint.address() != address);

		endpoint.address(address);
		CHECK(endpoint.address() == address);
	}

	SECTION("port")
	{
		endpoint_type endpoint(protocol, port);
		CHECK(endpoint.port() == port);

		endpoint.port(port + 1);
		CHECK(endpoint.port() == port + 1);
	}

	SECTION("data")
	{
		endpoint_type endpoint(protocol, port);
		if (endpoint.address().is_v4())
		{
			auto sa = static_cast<sockaddr_in *>(endpoint.data());
			sa->sin_port = htons(port + 1);
			CHECK(const_cast<const endpoint_type &>(endpoint).data() == sa);
		}
		else
		{
			auto sa = static_cast<sockaddr_in6 *>(endpoint.data());
			sa->sin6_port = htons(port + 1);
			CHECK(const_cast<const endpoint_type &>(endpoint).data() == sa);
		}
		CHECK(endpoint.port() == port + 1);
	}

	SECTION("size")
	{
		endpoint_type endpoint(protocol, port);
		if (protocol == TestType::v4())
		{
			CHECK(endpoint.size() == sizeof(sockaddr_in));
		}
		else
		{
			CHECK(endpoint.size() == sizeof(sockaddr_in6));
		}
	}

	SECTION("resize")
	{
		endpoint_type endpoint(protocol, port);

		std::error_code error;
		endpoint.resize(endpoint.size(), error);
		CHECK(!error);
		CHECK_NOTHROW(endpoint.resize(endpoint.size()));

		endpoint.resize(endpoint.size() * 2, error);
		CHECK(error == pal::net::socket_errc::address_length_error);
		CHECK_THROWS_AS(
			endpoint.resize(endpoint.size() * 2),
			std::length_error
		);
	}

	SECTION("capacity")
	{
		endpoint_type endpoint(protocol, port);
		CHECK(endpoint.capacity() == sizeof(endpoint));
	}

	SECTION("hash")
	{
		endpoint_type ep1(address, port);
		endpoint_type ep2(address, port + 1);
		CHECK(ep1.hash() != ep2.hash());
	}

	SECTION("to_chars")
	{
		endpoint_type endpoint(address, port);

		CHECK(endpoint.address() == address);

		char buf[INET6_ADDRSTRLEN + sizeof("[]:65535")];
		auto [p, ec] = endpoint.to_chars(buf, buf + sizeof(buf));
		CHECK(ec == std::errc{});
		CHECK(std::string(buf, p) == c_str);
	}

	SECTION("to_chars failure")
	{
		endpoint_type endpoint(address, port);

		const size_t max_size = strlen(c_str), min_size = 0;
		auto buf_size = GENERATE_COPY(range(min_size, max_size));

		std::string buf(buf_size, '\0');
		auto [p, ec] = endpoint.to_chars(buf.data(), buf.data() + buf.size());
		CHECK(p == buf.data() + buf.size());
		CHECK(ec == std::errc::value_too_large);
	}

	SECTION("to_string")
	{
		endpoint_type endpoint(address, port);
		CHECK(endpoint.to_string() == c_str);
	}

	SECTION("ostream")
	{
		endpoint_type endpoint(address, port);
		std::ostringstream oss;
		oss << endpoint;
		CHECK(oss.str() == c_str);
	}
}


} // namespace
