#include <pal/net/ip/port_type.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <span>
#include <string_view>

namespace
{

using namespace pal::net::ip;

struct to_port_type_case
{
	std::string_view name;
	port_type port;
};

constexpr to_port_type_case to_port_type_success[] = {
	{"HTTP", port_type::http},
	{"Https", port_type::https},
	{"sTuN", port_type::stun},
	{"TURN", port_type::stun},
	{"Turns", port_type::stuns},
	{"FTP_DATA", port_type::ftp_data},
};

constexpr std::string_view to_port_type_failure[] = {
	"",
	"htt",
	"httpx",
	"http ",
	"8080",
	"[ttp",
	"@ttp",
};

TEST_CASE("net/ip/port_type")
{

	SECTION("named values")
	{
		#define __pal_net_ip_port_type_value(Name, Value) static_assert(port_type::Name == port_type{(Value)});
		#define __pal_net_ip_port_type_alias(Name, Target) static_assert(port_type::Name == port_type::Target);
			__pal_net_ip_port_type(__pal_net_ip_port_type_value, __pal_net_ip_port_type_alias)
		#undef __pal_net_ip_port_type_alias
		#undef __pal_net_ip_port_type_value
	}

	SECTION("to_string_view")
	{
		CHECK(to_string_view(port_type{8080}).empty());
		#define __pal_net_ip_port_type_value(Name, Value) CHECK(to_string_view(port_type::Name) == #Name);
		#define __pal_net_ip_port_type_alias(Name, Target) CHECK(to_string_view(port_type::Name) == #Target);
			__pal_net_ip_port_type(__pal_net_ip_port_type_value, __pal_net_ip_port_type_alias)
		#undef __pal_net_ip_port_type_alias
		#undef __pal_net_ip_port_type_value
	}

	SECTION("to_port_type: names")
	{
		#define __pal_net_ip_port_type_value(Name, Value) static_assert(to_port_type(#Name) == port_type::Name);
			__pal_net_ip_port_type(__pal_net_ip_port_type_value, __pal_net_ip_port_type_value)
		#undef __pal_net_ip_port_type_value
	}

	SECTION("to_port_type: success")
	{
		const auto &[name, port] = GENERATE(from_range(std::span{to_port_type_success}));
		CAPTURE(name);
		CHECK(to_port_type(name) == port);
	}

	SECTION("to_port_type: failure")
	{
		const auto &name = GENERATE(from_range(std::span{to_port_type_failure}));
		CAPTURE(name);
		CHECK(to_port_type(name) == port_type::unspecified);
	}

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
