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
	{"http", port_type::http},
	{"turn", port_type::stun},
	{"turns", port_type::stuns},
	{"ftp_data", port_type::ftp_data},
	{"0", port_type::unspecified},
	{"80", port_type::http},
	{"0080", port_type::http},
	{"0000000000000000000080", port_type::http},
	{"8080", port_type{8080}},
	{"65535", port_type{65535}},
};

constexpr std::string_view to_port_type_failure[] = {
	"",
	"HTTP",
	"Turn",
	"htt",
	"httpx",
	"http ",
	"[ttp",
	"@ttp",
	"65536",
	"-1",
	"+80",
	" 80",
	"80 ",
	"80x",
	"0x50",
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
		#define __pal_net_ip_port_type_value(Name, Value) \
			static_assert(to_port_type(#Name) == port_type::Name); \
			CHECK(to_port_type(#Name) == port_type::Name);
			__pal_net_ip_port_type(__pal_net_ip_port_type_value, __pal_net_ip_port_type_value)
		#undef __pal_net_ip_port_type_value
	}

	SECTION("to_port_type: numbers")
	{
		static_assert(to_port_type("0") == port_type::unspecified);
		static_assert(to_port_type("8080") == port_type{8080});
		static_assert(to_port_type("65535") == port_type{65535});
		static_assert(!to_port_type("65536"));
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
		CHECK_FALSE(to_port_type(name));
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

	SECTION("arithmetic: negative")
	{
		static_assert(port_type::http + -1 == port_type{79});
		static_assert(port_type::http - -1 == port_type{81});
	}

	SECTION("arithmetic: truncate")
	{
		static_assert(port_type::http + 65536 == port_type::http);
		static_assert(port_type::http - 65536ULL == port_type::http);
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
		CHECK(std::format("{:n}", port_type::turn) == "stun");
	}

	SECTION("format: parse to end")
	{
		std::formatter<port_type> numeric;
		std::format_parse_context numeric_ctx{""};
		CHECK(numeric.parse(numeric_ctx) == numeric_ctx.end());
		CHECK_FALSE(numeric.named_);

		std::formatter<port_type> named;
		std::format_parse_context named_ctx{"n"};
		CHECK(named.parse(named_ctx) == named_ctx.end());
		CHECK(named.named_);
	}

	SECTION("format: invalid")
	{
		auto p = port_type::http;
		CHECK_THROWS_AS((void)std::vformat("{:x}", std::make_format_args(p)), std::format_error);
	}
}

} // namespace
