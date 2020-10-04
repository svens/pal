#include <pal/net/ip/tcp>
#include <pal/net/ip/udp>
#include <pal/net/test>
#include <sstream>


namespace {


using tcp = pal::net::ip::tcp;
using udp = pal::net::ip::udp;


TEMPLATE_TEST_CASE("net/ip/endpoint", "", tcp, udp)
{
	using endpoint_type = typename TestType::endpoint;
	using protocol_type = typename endpoint_type::protocol_type;

	using address_v4 = pal::net::ip::address_v4;
	using address_v6 = pal::net::ip::address_v6;

	constexpr pal::net::ip::port_type port = 80;

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

	auto [protocol, address, as_string] = GENERATE(
		table<protocol_type, pal::net::ip::address, std::string>({
		{ protocol_type::v4(), address_v4::loopback(), "127.0.0.1:80" },
		{ protocol_type::v6(), address_v6::loopback(), "[::1]:80" },
	}));
	CAPTURE(protocol.family());
	CAPTURE(address);
	CAPTURE(port);

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
		if (protocol == protocol_type::v4())
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

	SECTION("to_chars failure")
	{
		endpoint_type endpoint(address, port);

		const size_t max_size = as_string.size(), min_size = 0;
		auto buf_size = GENERATE_COPY(range(min_size, max_size));

		std::string buf(buf_size, '\0');
		auto [p, ec] = endpoint.to_chars(buf.data(), buf.data() + buf.size());
		CHECK(p == buf.data() + buf.size());
		CHECK(ec == std::errc::value_too_large);
	}

	SECTION("to_string")
	{
		endpoint_type endpoint(address, port);
		CHECK(endpoint.to_string() == as_string);
	}

	SECTION("ostream")
	{
		endpoint_type endpoint(address, port);
		std::ostringstream oss;
		oss << endpoint;
		CAPTURE(oss.str().size());
		CHECK(oss.str() == as_string);
	}
}


} // namespace
