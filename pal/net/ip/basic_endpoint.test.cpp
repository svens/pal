#include <pal/net/ip/basic_endpoint>
#include <pal/net/ip/tcp>
#include <pal/net/ip/udp>
#include <pal/net/test>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

namespace {

using namespace pal::net::ip;

TEMPLATE_TEST_CASE("net/ip/basic_endpoint", "", tcp, udp)
{
	using E = typename TestType::endpoint;
	using A = address;
	using A4 = address_v4;
	using A6 = address_v6;

	constexpr port_type port = 60000;
	constexpr size_t capacity = (std::max)(sizeof(::sockaddr_in), sizeof(::sockaddr_in6));

	SECTION("constexpr")
	{
		// static_assert -> CHECK: can't be made constexpr
		// see basic_endpoint<>

		constexpr E e1;
		static_assert(e1.protocol() == TestType::v4);
		static_assert(e1.port() == 0);
		static_assert(e1.size() == sizeof(::sockaddr_in));
		static_assert(e1.capacity() == capacity);
		static_assert(e1.address() == A4::any());

		constexpr E e2{TestType::v4, port};
		static_assert(e2.protocol() == TestType::v4);
		static_assert(e2.port() == port);
		static_assert(e2.size() == sizeof(::sockaddr_in));
		static_assert(e2.capacity() == capacity);
		static_assert(e2.address() == A4::any());

		constexpr E e3{TestType::v6, port};
		CHECK(e3.protocol() == TestType::v6);
		CHECK(e3.port() == port);
		CHECK(e3.size() == sizeof(::sockaddr_in6));
		static_assert(e3.capacity() == capacity);
		CHECK(e3.address() == A6::any());

		constexpr E e4{A4::loopback(), port};
		static_assert(e4.protocol() == TestType::v4);
		static_assert(e4.port() == port);
		static_assert(e4.size() == sizeof(::sockaddr_in));
		static_assert(e4.capacity() == capacity);
		static_assert(e4.address() == A4::loopback());

		constexpr E e5{A6::loopback(), port};
		CHECK(e5.protocol() == TestType::v6);
		CHECK(e5.port() == port);
		CHECK(e5.size() == sizeof(::sockaddr_in6));
		static_assert(e5.capacity() == capacity);
		CHECK(e5.address() == A6::loopback());

		constexpr A loopback_v4{A4::loopback()};
		constexpr E e6{loopback_v4, port};
		static_assert(e6.protocol() == TestType::v4);
		static_assert(e6.port() == port);
		static_assert(e6.size() == sizeof(::sockaddr_in));
		static_assert(e6.capacity() == capacity);
		static_assert(e6.address() == loopback_v4);

		constexpr A loopback_v6{A6::loopback()};
		constexpr E e7{loopback_v6, port};
		CHECK(e7.protocol() == TestType::v6);
		CHECK(e7.port() == port);
		CHECK(e7.size() == sizeof(::sockaddr_in6));
		static_assert(e7.capacity() == capacity);
		CHECK(e7.address() == loopback_v6);
	}

	SECTION("ctor")
	{
		E e;
		CHECK(e.protocol() == TestType::v4);
		CHECK(e.address() == A4::any());
		CHECK(e.port() == 0);
	}

	SECTION("ctor(address_v4)")
	{
		E e{A4::loopback(), port};
		CHECK(e.protocol() == TestType::v4);
		CHECK(e.address() == A4::loopback());
		CHECK(e.port() == port);
	}

	SECTION("ctor(address_v6)")
	{
		E e{A6::loopback(), port};
		CHECK(e.protocol() == TestType::v6);
		CHECK(e.address() == A6::loopback());
		CHECK(e.port() == port);
	}

	SECTION("compare")
	{
		auto [a, b] = GENERATE(table<A, A>({
			{ A4::any(), A4::loopback() },
			{ A6::any(), A6::loopback() },
			{ A4::any(), A6::loopback() },
		}));
		CAPTURE(std::format("{}", a));
		CAPTURE(std::format("{}", b));

		E e1{a, port}, e2{b, port};
		auto e3 = e1;

		CHECK(e1 != e2);
		CHECK(e1 == e3);

		CHECK(e1 < e2);
		CHECK(e1 <= e2);
		CHECK(e1 <= e3);

		CHECK_FALSE(e1 > e2);
		CHECK_FALSE(e1 >= e2);
		CHECK(e1 >= e3);
	}

	auto [view, protocol, address] = GENERATE(table<std::string, TestType, A>({
		{ "0.0.0.0:60000", TestType::v4, A4::any() },
		{ "127.0.0.1:60000", TestType::v4, A4::loopback() },
		{ "[::]:60000", TestType::v6, A6::any() },
		{ "[::1]:60000", TestType::v6, A6::loopback() },
		{ "[ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff]:60000", TestType::v6, A6{A6::bytes_type{255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255}} },
	}));
	CAPTURE(view);

	SECTION("ctor(protocol)")
	{
		E e{protocol, port};
		CHECK(e.protocol() == protocol);
		CHECK(e.address().is_unspecified());
		CHECK(e.port() == port);
	}

	SECTION("ctor(address)")
	{
		E e{address, port};
		CHECK(e.protocol() == protocol);
		CHECK(e.address() == address);
		CHECK(e.port() == port);
	}

	SECTION("address")
	{
		E e{A4::broadcast(), port};
		e.address(address);
		CHECK(e.address() == address);
		CHECK(e.port() == port);
	}

	SECTION("port")
	{
		E e{address, port};
		e.port(port + 1);
		CHECK(e.address() == address);
		CHECK(e.port() == port + 1);
	}

	SECTION("data")
	{
		E e{protocol, port};
		if (std::holds_alternative<A4>(e.address()))
		{
			auto sa = static_cast<::sockaddr_in *>(e.data());
			sa->sin_port = htons(port + 1);
			CHECK(const_cast<const E &>(e).data() == sa);
		}
		else
		{
			auto sa = static_cast<::sockaddr_in6 *>(e.data());
			sa->sin6_port = htons(port + 1);
			CHECK(const_cast<const E &>(e).data() == sa);
		}
		CHECK(e.port() == port + 1);
	}

	SECTION("size and capacity")
	{
		E e{protocol, port};
		if (protocol == TestType::v4)
		{
			CHECK(e.size() == sizeof(::sockaddr_in));
		}
		else
		{
			CHECK(e.size() == sizeof(::sockaddr_in6));
		}
		CHECK(e.capacity() >= e.size());
		CHECK(e.capacity() == capacity);
	}

	SECTION("resize")
	{
		E e{protocol, port};

		auto r = e.resize(e.size());
		CHECK(r);

		r = e.resize(e.size() + 1);
		REQUIRE_FALSE(r);
		CHECK(r.error() == std::errc::invalid_argument);
	}

	SECTION("hash")
	{
		E e1{address, port}, e2{address, port + 1};
		CHECK(e1.hash() != e2.hash());
	}

	SECTION("to_chars")
	{
		E e{address, port};
		char buf[INET6_ADDRSTRLEN + sizeof("[]:65535")];
		auto [p, ec] = e.to_chars(buf, buf + sizeof(buf));
		REQUIRE(ec == std::errc{});
		CHECK(std::string{buf, p} == view);
	}

	SECTION("to_chars: failure")
	{
		E e{address, port};
		const size_t min_size = 0, max_size = view.size();
		auto buf_size = GENERATE_COPY(range(min_size, max_size));
		std::string buf(buf_size, '\0');
		auto [p, ec] = e.to_chars(buf.data(), buf.data() + buf.size());
		CHECK(p == buf.data() + buf.size());
		CHECK(ec == std::errc::value_too_large);
	}

	SECTION("format")
	{
		E e{address, port};
		CHECK(std::format("{}", e) == view);
	}
}

} // namespace
