#include <pal/net/ip/port_type.hpp>
#include <catch2/catch_test_macros.hpp>

namespace
{

using namespace pal::net::ip;

TEST_CASE("net/ip/port_type")
{
	// clang-format off

	SECTION("named values")
	{
		#define check_value(Name, Value) static_assert(port_type::Name == port_type{(Value)});
			__pal_net_ip_port_type(check_value)
		#undef check_value
	}

	SECTION("to_string_view")
	{
		CHECK(to_string_view(port_type{8080}).empty());
		#define check_sv(Name, Value) CHECK(to_string_view(port_type::Name) == #Name);
			__pal_net_ip_port_type(check_sv)
		#undef check_sv
	}

	// clang-format on

	SECTION("arithmetic")
	{
		constexpr auto p = port_type::http;

		static_assert(p + 1 == port_type{81});
		static_assert(p - 1 == port_type{79});

		auto q = p;
		CHECK(++q == port_type{81});
		CHECK(q == port_type{81});

		CHECK(q++ == port_type{81});
		CHECK(q == port_type{82});

		CHECK(--q == port_type{81});
		CHECK(q == port_type{81});

		CHECK(q-- == port_type{81});
		CHECK(q == port_type{80});
	}

	SECTION("arithmetic: wrap")
	{
		static_assert(port_type{65535} + 1 == port_type::unspecified);
		static_assert(port_type::unspecified - 1 == port_type{65535});
	}

	SECTION("format: numeric")
	{
		CHECK(std::format("{}", port_type::http) == "80");
		CHECK(std::format("{}", port_type{8080}) == "8080");
		CHECK(std::format("{}", port_type::unspecified) == "0");
		auto http = port_type::http;
		CHECK(std::vformat("{}", std::make_format_args(http)) == "80");
	}

	SECTION("format: named")
	{
		CHECK(std::format("{:n}", port_type::http) == "http");
		CHECK(std::format("{:n}", port_type::https) == "https");
		CHECK(std::format("{:n}", port_type{8080}) == "8080");
	}

	SECTION("format: invalid")
	{
		auto p = port_type::http;
		CHECK_THROWS_AS((void)std::vformat("{:x}", std::make_format_args(p)), std::format_error);
	}
}

} // namespace
