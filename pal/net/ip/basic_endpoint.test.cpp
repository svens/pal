#include <pal/net/ip/tcp.hpp>
#include <pal/net/ip/udp.hpp>
#include <pal/masked_formatter.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

namespace
{

using namespace pal::net::ip;

TEMPLATE_TEST_CASE("net/ip/basic_endpoint", "", tcp, udp)
{
	using E = TestType::endpoint;
	using A = address;
	using A4 = address_v4;
	using A6 = address_v6;

	constexpr auto port = port_type{60000};
	constexpr size_t capacity = (std::max)(sizeof(::sockaddr_in), sizeof(::sockaddr_in6));

	SECTION("constexpr")
	{
		// v6 paths below use CHECK instead of static_assert: reading family()
		// via v4_.sin_family is undefined in constant evaluation when v6_ is
		// the active union member (runtime behaviour is correct via the common
		// initial sequence)

		constexpr E e1;
		static_assert(e1.protocol() == TestType::v4);
		static_assert(e1.port() == port_type::unspecified);
		static_assert(e1.size() == sizeof(::sockaddr_in));
		static_assert(e1.capacity() == capacity);
		static_assert(e1.address() == A4::any);

		constexpr E e2{TestType::v4, port};
		static_assert(e2.protocol() == TestType::v4);
		static_assert(e2.port() == port);
		static_assert(e2.size() == sizeof(::sockaddr_in));
		static_assert(e2.capacity() == capacity);
		static_assert(e2.address() == A4::any);

		constexpr E e3{TestType::v6, port};
		CHECK(e3.protocol() == TestType::v6);
		CHECK(e3.port() == port);
		CHECK(e3.size() == sizeof(::sockaddr_in6));
		static_assert(e3.capacity() == capacity);
		CHECK(e3.address() == A6::any);

		constexpr E e4{A4::loopback, port};
		static_assert(e4.protocol() == TestType::v4);
		static_assert(e4.port() == port);
		static_assert(e4.size() == sizeof(::sockaddr_in));
		static_assert(e4.capacity() == capacity);
		static_assert(e4.address() == A4::loopback);

		constexpr E e5{A6::loopback, port};
		CHECK(e5.protocol() == TestType::v6);
		CHECK(e5.port() == port);
		CHECK(e5.size() == sizeof(::sockaddr_in6));
		static_assert(e5.capacity() == capacity);
		CHECK(e5.address() == A6::loopback);

		constexpr A loopback_v4{A4::loopback};
		constexpr E e6{loopback_v4, port};
		static_assert(e6.protocol() == TestType::v4);
		static_assert(e6.port() == port);
		static_assert(e6.size() == sizeof(::sockaddr_in));
		static_assert(e6.capacity() == capacity);
		static_assert(e6.address() == loopback_v4);

		constexpr A loopback_v6{A6::loopback};
		constexpr E e7{loopback_v6, port};
		CHECK(e7.protocol() == TestType::v6);
		CHECK(e7.port() == port);
		CHECK(e7.size() == sizeof(::sockaddr_in6));
		static_assert(e7.capacity() == capacity);
		CHECK(e7.address() == loopback_v6);
	}

	SECTION("ctor")
	{
		const E e;
		CHECK(e.protocol() == TestType::v4);
		CHECK(e.address() == A4::any);
		CHECK(e.port() == port_type::unspecified);
	}

	SECTION("ctor(address_v4)")
	{
		const E e{A4::loopback, port};
		CHECK(e.protocol() == TestType::v4);
		CHECK(e.address() == A4::loopback);
		CHECK(e.port() == port);
	}

	SECTION("ctor(address_v6)")
	{
		const E e{A6::loopback, port};
		CHECK(e.protocol() == TestType::v6);
		CHECK(e.address() == A6::loopback);
		CHECK(e.port() == port);
	}

	SECTION("compare")
	{
		auto [a, b] = GENERATE(
			table<A, A>({
				{A4::any, A4::loopback},
				{A6::any, A6::loopback},
				{A4::any, A6::loopback},
			})
		);
		CAPTURE(std::format("{}", a));
		CAPTURE(std::format("{}", b));

		const E e1{a, port}, e2{b, port};
		const auto e3 = e1;

		CHECK(e1 != e2);
		CHECK(e1 == e3);

		CHECK(e1 < e2);
		CHECK(e1 <= e2);
		CHECK(e1 <= e3);

		CHECK_FALSE(e1 > e2);
		CHECK_FALSE(e1 >= e2);
		CHECK(e1 >= e3);
	}

	// clang-format off
	auto [view, protocol, addr] = GENERATE(table<std::string, TestType, A>({
		{"0.0.0.0:60000", TestType::v4, A4::any},
		{"127.0.0.1:60000", TestType::v4, A4::loopback},
		{"[::]:60000", TestType::v6, A6::any},
		{"[::1]:60000", TestType::v6, A6::loopback},
		{"[ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff]:60000", TestType::v6, A6{A6::bytes_type{255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255}}},
	}));
	// clang-format on

	CAPTURE(view);

	SECTION("ctor(protocol)")
	{
		const E e{protocol, port};
		CHECK(e.protocol() == protocol);
		CHECK(e.address().is_unspecified());
		CHECK(e.port() == port);
	}

	SECTION("ctor(address)")
	{
		const E e{addr, port};
		CHECK(e.protocol() == protocol);
		CHECK(e.address() == addr);
		CHECK(e.port() == port);
	}

	SECTION("address")
	{
		E e{A4::broadcast, port};
		e.address(addr);
		CHECK(e.address() == addr);
		CHECK(e.port() == port);
	}

	SECTION("port")
	{
		E e{addr, port};
		e.port(port + 1);
		CHECK(e.address() == addr);
		CHECK(e.port() == port + 1);
	}

	SECTION("data")
	{
		E e{protocol, port};
		if (e.address().is_v4())
		{
			auto *sa = static_cast<::sockaddr_in *>(e.data());
			sa->sin_port = htons(static_cast<uint16_t>(port + 1));
			CHECK(const_cast<const E &>(e).data() == sa);
		}
		else
		{
			auto *sa = static_cast<::sockaddr_in6 *>(e.data());
			sa->sin6_port = htons(static_cast<uint16_t>(port + 1));
			CHECK(const_cast<const E &>(e).data() == sa);
		}
		CHECK(e.port() == port + 1);
	}

	SECTION("size and capacity")
	{
		const E e{protocol, port};
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
		const E e1{addr, port}, e2{addr, port + 1};
		CHECK(e1.hash() != e2.hash());
	}

	SECTION("to_chars")
	{
		const E e{addr, port};
		char buf[INET6_ADDRSTRLEN + sizeof("[]:65535")];
		auto [p, ec] = e.to_chars(buf, buf + sizeof(buf));
		REQUIRE(ec == std::errc{});
		CHECK(std::string{buf, p} == view);
	}

	SECTION("to_chars: failure")
	{
		const E e{addr, port};
		const size_t min_size = 0, max_size = view.size();
		auto buf_size = GENERATE_COPY(range(min_size, max_size));
		std::string buf(buf_size, '\0');
		auto [p, ec] = e.to_chars(buf.data(), buf.data() + buf.size());
		CHECK(p == buf.data() + buf.size());
		CHECK(ec == std::errc::value_too_large);
	}

	SECTION("format")
	{
		const E e{addr, port};
		CHECK(std::format("{}", e) == view);
		CHECK_THROWS_AS((void)std::vformat("{:x}", std::make_format_args(e)), std::format_error);
	}

	SECTION("masked")
	{
		const E e{addr, port};
		std::string expected;
		if (const auto *v4 = addr.v4())
		{
			const auto &b = v4->to_bytes();
			expected = std::format("{}.{}.{}.0:{}", b[0], b[1], b[2], port);
		}
		else
		{
			auto bytes = addr.v6()->to_bytes();
			std::fill(bytes.begin() + 8, bytes.end(), uint8_t{});
			std::array<char, A6::max_string_length + 1> buf{};
			auto [end, _] = A6{bytes}.to_chars(buf.data(), buf.data() + buf.size());
			expected = std::format("[{}]:{}", std::string_view{buf.data(), end}, port);
		}
		CHECK(std::format("{}", pal::masked{e}) == expected);
	}
}

} // namespace
