#include <pal/net/ip/network.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

namespace
{

TEST_CASE("net/ip/network")
{
	using pal::net::ip::make_network;
	using pal::net::ip::make_network_v4;
	using pal::net::ip::make_network_v6;
	using N = pal::net::ip::network;
	using A = pal::net::ip::address;
	using A4 = pal::net::ip::address_v4;
	using A6 = pal::net::ip::address_v6;
	using B4 = A4::bytes_type;
	using B6 = A6::bytes_type;

	SECTION("constexpr")
	{
		constexpr N n;
		static_assert(n.is_v4());
		static_assert(!n.is_v6());
		static_assert(n.v4() != nullptr);
		static_assert(n.v6() == nullptr);
		static_assert(n.prefix_length() == 0);

		constexpr auto v6_net = make_network_v6(A6::any, 0);
		static_assert(v6_net.has_value());
		constexpr N n6{*v6_net};
		static_assert(n6.is_v6());
		static_assert(!n6.is_v4());
		static_assert(n6.v6() != nullptr);
		static_assert(n6.v4() == nullptr);
		static_assert(n6.prefix_length() == 0);

		constexpr N c{n6};
		static_assert(n6 == c);
		static_assert(c != n);
		static_assert(n < c); // v4 < v6
		static_assert(n <= c);
		static_assert(!(c < n));
		static_assert(!(c <= n));

		static_assert(n.hash() != 0);
		static_assert(n6.hash() == c.hash());
	}

	SECTION("compare")
	{
		N any_v4{*make_network_v4(A4::any, 0)};
		N any_v6{*make_network_v6(A6::any, 0)};
		N loopback_v4{*make_network_v4(A4::loopback, 32)};
		N loopback_v6{*make_network_v6(A6::loopback, 128)};

		// same-family
		CHECK(any_v4 < loopback_v4);
		CHECK(any_v4 != loopback_v4);
		CHECK(any_v6 < loopback_v6);
		CHECK(any_v6 != loopback_v6);

		// cross-family: all v4 < all v6
		CHECK(any_v4 < any_v6);
		CHECK(any_v4 < loopback_v6);
		CHECK(loopback_v4 < any_v6);
		CHECK(any_v4 != any_v6);
		CHECK_FALSE(any_v6 < any_v4);
		CHECK_FALSE(any_v6 <= any_v4);
		CHECK(any_v6 > any_v4);
		CHECK(any_v6 >= any_v4);
	}

	SECTION("contains cross-family")
	{
		auto n4 = N{*make_network_v4(B4{10, 0, 0, 0}, 8)};
		auto n6 = N{*make_network_v6(A6::any, 0)};

		CHECK(n4.contains(A{B4{10, 1, 2, 3}}));
		CHECK_FALSE(n4.contains(A{A6::loopback})); // v4 network, v6 address
		CHECK(n6.contains(A{A6::loopback}));
		CHECK_FALSE(n6.contains(A{A4::loopback})); // v6 network, v4 address
	}

	SECTION("is_subnet_of cross-family")
	{
		auto n4 = N{*make_network_v4(A4::any, 0)};
		auto n6 = N{*make_network_v6(A6::any, 0)};
		auto sub4 = N{*make_network_v4(B4{10, 0, 0, 0}, 8)};
		auto sub6 = N{*make_network_v6(A6::loopback, 128)};

		CHECK(sub4.is_subnet_of(n4));
		CHECK_FALSE(sub4.is_subnet_of(n6)); // cross-family
		CHECK_FALSE(n4.is_subnet_of(n6));   // cross-family
		CHECK(sub6.is_subnet_of(n6));
		CHECK_FALSE(n6.is_subnet_of(n4)); // cross-family
	}

	// clang-format off
	auto [view, net, is_v4] = GENERATE(table<std::string, N, bool>({
		{ "0.0.0.0/0", N{*make_network_v4(A4::any, 0)}, true },
		{ "192.168.0.0/16", N{*make_network_v4(B4{192,168,0,0}, 16)}, true },
		{ "::/0", N{*make_network_v6(A6::any, 0)}, false },
		{ "2001:db8::/32", N{*make_network_v6(B6{0x20,0x01,0x0d,0xb8,0,0,0,0,0,0,0,0,0,0,0,0}, 32)}, false },
	}));
	// clang-format on

	CAPTURE(view);

	SECTION("copy ctor")
	{
		N n{net};
		CHECK(n == net);
	}

	SECTION("move ctor")
	{
		N src{net};
		N n{std::move(src)};
		CHECK(n == net);
	}

	SECTION("copy assign")
	{
		N n;
		n = net;
		CHECK(n == net);

		N b;
		if (const auto *p4 = n.v4())
		{
			b = *p4;
		}
		else
		{
			b = *n.v6();
		}
		CHECK(n == b);
	}

	SECTION("move assign")
	{
		N src{net};
		N n;
		n = std::move(src);
		CHECK(n == net);
	}

	SECTION("properties")
	{
		CHECK(net.is_v4() == is_v4);
		CHECK(net.is_v6() == !is_v4);
		CHECK((net.v4() != nullptr) == is_v4);
		CHECK((net.v6() != nullptr) == !is_v4);
		const auto expected_prefix = static_cast<uint8_t>(std::stoi(view.substr(view.rfind('/') + 1)));
		CHECK(net.prefix_length() == expected_prefix);
	}

	SECTION("to_chars")
	{
		std::array<char, N::max_string_length + 1> buf{};
		auto [p, ec] = net.to_chars(buf.data(), buf.data() + buf.size());
		REQUIRE(ec == std::errc{});
		CHECK(std::string{buf.data(), p} == view);
	}

	SECTION("to_chars failure")
	{
		const size_t max_size = view.size(), min_size = 0;
		auto buf_size = GENERATE_COPY(range(min_size, max_size));
		std::string buf(buf_size, '\0');
		auto [p, ec] = net.to_chars(buf.data(), buf.data() + buf.size());
		CHECK(ec == std::errc::value_too_large);
		CHECK(p == buf.data() + buf.size());
	}

	SECTION("from_chars")
	{
		N n;
		auto [p, ec] = n.from_chars(view.data(), view.data() + view.size());
		REQUIRE(ec == std::errc{});
		CHECK(p == view.data() + view.size());
		CHECK(n == net);
		CHECK(n.is_v4() == is_v4);
	}

	SECTION("from_chars streaming")
	{
		const auto input = view + ",next";
		N n;
		auto [p, ec] = n.from_chars(input.data(), input.data() + input.size());
		REQUIRE(ec == std::errc{});
		CHECK(*p == ',');
		CHECK(n == net);
	}

	SECTION("from_chars failure")
	{
		const auto bad = 'x' + view;
		N n;
		const auto r = n.from_chars(bad.data(), bad.data() + bad.size());
		CHECK(r.ec == std::errc::invalid_argument);
		CHECK(r.ptr == bad.data());
	}

	SECTION("hash")
	{
		CHECK(net.hash() != 0);
		CHECK(std::hash<N>{}(net) == net.hash());
	}

	SECTION("make_network(string_view)")
	{
		auto n = make_network(view);
		REQUIRE(n);
		CHECK(*n == net);
	}

	SECTION("make_network(string_view) failure")
	{
		auto bad = view;
		bad.append(N::max_string_length, 'x');
		auto n = make_network(bad);
		REQUIRE_FALSE(n);
		CHECK(n.error() == std::make_error_code(std::errc::invalid_argument));
	}

	SECTION("format")
	{
		CHECK(std::format("{}", net) == view);
		CHECK_THROWS_AS((void)std::vformat("{:x}", std::make_format_args(net)), std::format_error);
	}
}

} // namespace
