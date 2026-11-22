#include <pal/uri.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <span>
#include <string>
#include <string_view>

namespace
{

using pal::net::ip::port_type;

constexpr auto address_1234 = pal::net::ip::make_address_v4(pal::net::ip::address_v4::bytes_type{1, 2, 3, 4});
constexpr auto scoped_loopback = pal::net::ip::make_address_v6(pal::net::ip::address_v6::loopback.to_bytes(), 1);

struct parse_case
{
	std::string_view text;
	pal::uri_view expected;
	std::string_view canonical{};
};

constexpr parse_case parse_success[] =
{
	{"", {}},
	{std::string_view{}, {}},
	{"//", {.host = ""}},
	{"a://", {.scheme = "a", .host = ""}},
	{"//@", {.userinfo = "", .host = ""}},
	{"s://:@h", {.scheme = "s", .userinfo = ":", .host = "h"}},
	{"//h:80?q", {.host = "h", .port = port_type::http, .query = "q"}},
	{"http://h:0000000000000000000080", {.scheme = "http", .host = "h", .port = port_type::http}, "http://h:80"},
	{"http://example.com", {.scheme = "http", .host = "example.com"}},
	{"http://example.com/", {.scheme = "http", .host = "example.com", .path = "/"}},
	{
		"HTTP://user:pass@Example.com:8080/a/b?q=1#f",
		{
			.scheme = "HTTP",
			.userinfo = "user:pass",
			.host = "Example.com",
			.port = port_type{8080},
			.path = "/a/b",
			.query = "q=1",
			.fragment = "f",
		},
	},
	{"http://h?", {.scheme = "http", .host = "h", .query = ""}},
	{"http://h#", {.scheme = "http", .host = "h", .fragment = ""}},
	{"http://h?#", {.scheme = "http", .host = "h", .query = "", .fragment = ""}},
	{"http://@h", {.scheme = "http", .userinfo = "", .host = "h"}},
	{"s://u:p:q@h", {.scheme = "s", .userinfo = "u:p:q", .host = "h"}},
	{"http://h:", {.scheme = "http", .host = "h"}, "http://h"},
	{"http://h:0", {.scheme = "http", .host = "h", .port = port_type::unspecified}},
	{"http://h:65535", {.scheme = "http", .host = "h", .port = port_type{65535}}},
	{"http://h//x", {.scheme = "http", .host = "h", .path = "//x"}},
	{"http://%41b", {.scheme = "http", .host = "%41b"}},
	{"http://h/a%2Fb", {.scheme = "http", .host = "h", .path = "/a%2Fb"}},
	{"http://h?a=1&b=/?:@", {.scheme = "http", .host = "h", .query = "a=1&b=/?:@"}},
	{"http://h#a/?b", {.scheme = "http", .host = "h", .fragment = "a/?b"}},
	{"http://h?a#b?c", {.scheme = "http", .host = "h", .query = "a", .fragment = "b?c"}},
	{"http://1.2.3.4:80", {.scheme = "http", .host = "1.2.3.4", .port = port_type::http}},
	{"http://1.2.3.256", {.scheme = "http", .host = "1.2.3.256"}},
	{"http://[::1]:80/", {.scheme = "http", .host = "[::1]", .port = port_type::http, .path = "/"}},
	{"http://[2001:db8::1.2.3.4]", {.scheme = "http", .host = "[2001:db8::1.2.3.4]"}},
	{"http://[::1]:", {.scheme = "http", .host = "[::1]"}, "http://[::1]"},
	{"file:///etc/hosts", {.scheme = "file", .host = "", .path = "/etc/hosts"}},
	{"file:/etc/hosts", {.scheme = "file", .path = "/etc/hosts"}},
	{"mailto:user@example.com", {.scheme = "mailto", .path = "user@example.com"}},
	{"urn:a:b:c", {.scheme = "urn", .path = "a:b:c"}},
	{"a+b-c.d:x", {.scheme = "a+b-c.d", .path = "x"}},
	{"a:", {.scheme = "a"}},
	{
		"turn:example.org:3478?transport=udp",
		{.scheme = "turn", .path = "example.org:3478", .query = "transport=udp"},
	},
	{"//h", {.host = "h"}},
	{"//h/p", {.host = "h", .path = "/p"}},
	{"//:3478", {.host = "", .port = port_type::stun}},
	{"///x", {.host = "", .path = "/x"}},
	{"/a/b", {.path = "/a/b"}},
	{"a/b", {.path = "a/b"}},
	{"a/b:c", {.path = "a/b:c"}},
	{"./a:b", {.path = "./a:b"}},
	{"?q", {.query = "q"}},
	{"#f", {.fragment = "f"}},
	{"?", {.query = ""}},
	{"#", {.fragment = ""}},
	{
		"s://u:p@h:80/p?q#f",
		{
			.scheme = "s",
			.userinfo = "u:p",
			.host = "h",
			.port = port_type::http,
			.path = "/p",
			.query = "q",
			.fragment = "f",
		},
	},
};

constexpr std::string_view parse_failure[] =
{
	"1a:b",
	":x",
	"a b",
	"a%zz",
	"a%4g",
	"%",
	"http://h:65536",
	"http://h:99999999999",
	"http://h:8x",
	"http://h:-1",
	"http://h:1:2",
	"http://[::1",
	"http://[::1]x",
	"http://[::1]:x",
	"http://a@b@c",
	"http://us er@h",
	"http://h%zz",
	"http://h^",
	"http://h/a b",
	"http://h/[x]",
	"http://h?a b",
	"http://h#a#b",
	"turn:[::1]:3478",
	"a\xc3\xa4",
	"http://h\xff",
};

constexpr pal::uri_view format_failure[] =
{
	{.scheme = "1a"},
	{.scheme = ""},
	{.scheme = "a b"},
	{.userinfo = "u"},
	{.port = port_type::http},
	{.host = "h", .path = "x"},
	{.scheme = "a", .host = "h", .path = "x"},
	{.path = "//x"},
	{.scheme = "a", .path = "//x"},
	{.path = "a:b"},
	{.host = "h", .path = "/a b"},
	{.host = "a:b"},
	{.host = "h:1"},
	{.host = "[::1]x"},
	{.host = "[::1%1]"},
	{.scheme = "s", .userinfo = "u", .port = port_type::http},
	{.host = "%zz"},
	{.userinfo = "%zz", .host = "h"},
	{.userinfo = "a@b", .host = "h"},
	{.query = "%4"},
	{.path = "a b"},
	{.query = "#"},
	{.fragment = "#"},
};

struct host_case
{
	std::string_view text;
	bool ip;
	std::string_view rest;
};

constexpr host_case host_success[] =
{
	{"", false, ""},
	{std::string_view{}, false, ""},
	{"h", false, ""},
	{"h\xff", false, "\xff"},
	{"h:1", false, ":1"},
	{":3478", false, ":3478"},
	{"u@h", false, "@h"},
	{"h/x", false, "/x"},
	{"h h", false, " h"},
	{"a.b-c_d~e!$&'()*+,;=", false, ""},
	{"%41", false, ""},
	{"1.2.3", false, ""},
	{"1.2.3.4.5", false, ""},
	{"256.1.1.1", false, ""},
	{"1.2.3.04", false, ""},
	{"01.2.3.4", false, ""},
	{"1234.1.1.1", false, ""},
	{"1.2.3.", false, ""},
	{"1..3.4", false, ""},
	{".1.2.3", false, ""},
	{"1.2.3.4", true, ""},
	{"1.2.3.4:80", true, ":80"},
	{"0.0.0.0", true, ""},
	{"255.255.255.255", true, ""},
	{"[::]", true, ""},
	{"[::1]:3478", true, ":3478"},
	{"[::1]x", true, "x"},
	{"[::1]]", true, "]"},
	{"[1:2:3:4:5:6:7:8]", true, ""},
	{"[1:2:3:4:5:6:1.2.3.4]", true, ""},
	{"[::ffff:1.2.3.4]", true, ""},
	{"[1::]", true, ""},
	{"[1:2:3:4:5:6:7::]", true, ""},
	{"[::2:3:4:5:6:7:8]", true, ""},
	{"[1:2:3:4:5::1.2.3.4]", true, ""},
	{"[0001:0:0:0:0:0:0:1]", true, ""},
	{"[ABCD:ef01::]", true, ""},
	{"[1::8]", true, ""},
	{"[1:2:3:4:5:6::8]", true, ""},
	{"[::1:2:3:4:5:1.2.3.4]", true, ""},
	{"[1::2:3:4:5:1.2.3.4]", true, ""},
	{"[0000:0000:0000:0000:0000:0000:255.255.255.255]", true, ""},
};

constexpr std::string_view host_failure[] =
{
	"%zz",
	"h%4",
	"h%4g",
	"[::1",
	"[]",
	"[:]",
	"[:::]",
	"[1::2::3]",
	"[1:2:3:4:5:6:7:8:9]",
	"[1:2:3:4:5:6:7]",
	"[1:2:3:4:5:6:7:8::]",
	"[::1:2:3:4:5:6:7:8]",
	"[1:2:3:4:5:6::1.2.3.4]",
	"[:1::]",
	"[1:]",
	"[00001::]",
	"[g::]",
	"[1.2.3.4]",
	"[::1.2.3.4:1]",
	"[1.2.3.4::]",
	"[::01.2.3.4]",
	"[::1%25eth0]",
	"[::1%1]",
	"[v1.a]",
	"[1::2:]",
	"[1:::2]",
	"[::1.2.3]",
	"[1:2:3:4:5:6:7:1.2.3.4]",
};

struct host_format_case
{
	pal::uri_host host;
	std::string_view expected;
};

constexpr host_format_case host_format_success[] =
{
	{{.value = std::string_view{}}, ""},
	{{.value = std::string_view{"h"}}, "h"},
	{{.value = std::string_view{"%41.b"}}, "%41.b"},
	{{.value = std::string_view{"1.2.3"}}, "1.2.3"},
	{{.value = address_1234}, "1.2.3.4"},
	{{.value = pal::net::ip::address_v6::loopback}, "[::1]"},
	{{.value = pal::net::ip::make_address_v6(pal::net::ip::v4_mapped, address_1234)}, "[::ffff:1.2.3.4]"},
	{
		{.value = pal::net::ip::make_address_v6(pal::net::ip::address_v6::bytes_type{
			0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
			0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
		})},
		"[ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff]",
	},
};

constexpr pal::uri_host host_format_failure[] =
{
	{.value = std::string_view{"a:b"}},
	{.value = std::string_view{"a b"}},
	{.value = std::string_view{"[::1]"}},
	{.value = std::string_view{"%zz"}},
	{.value = std::string_view{"1.2.3.4"}},
	{.value = scoped_loopback},
};

template <typename T>
pal::result<T> check_make (std::string_view text)
{
	T value;
	auto [ptr, ec] = value.from_chars(text.data(), text.data() + text.size());
	auto made = T::make(text);
	if (ec == std::errc{} && ptr == text.data() + text.size())
	{
		REQUIRE(made);
		CHECK(*made == value);
	}
	else
	{
		CHECK(made.error() == std::errc::invalid_argument);
	}
	return made;
}

template <typename T>
std::string check_to_chars (const T &value)
{
	std::string buf(value.max_size(), '\0');
	auto [ptr, ec] = value.to_chars(buf.data(), buf.data() + buf.size());
	REQUIRE(ec == std::errc{});
	buf.resize(static_cast<size_t>(ptr - buf.data()));

	std::string exact(buf.size(), '\0');
	auto [exact_ptr, exact_ec] = value.to_chars(exact.data(), exact.data() + exact.size());
	REQUIRE(exact_ec == std::errc{});
	CHECK(exact_ptr == exact.data() + exact.size());
	CHECK(exact == buf);

	for (size_t size = 0; size < buf.size(); ++size)
	{
		CAPTURE(size);
		std::string small(size, '\0');
		auto [small_ptr, small_ec] = value.to_chars(small.data(), small.data() + small.size());
		CHECK(small_ec == std::errc::value_too_large);
		CHECK(small_ptr == small.data() + small.size());
	}

	return buf;
}

template <typename T>
void check_rejects (const T &value)
{
	std::string buf(value.max_size(), '\0');
	auto [ptr, ec] = value.to_chars(buf.data(), buf.data() + buf.size());
	CHECK(ec == std::errc::invalid_argument);
	CHECK(ptr == buf.data() + buf.size());
}

pal::uri_host expected_host (std::string_view consumed, bool ip)
{
	if (!ip)
	{
		return {.value = consumed};
	}
	if (consumed.starts_with('['))
	{
		consumed = consumed.substr(1, consumed.size() - 2);
	}
	return {.value = pal::net::ip::make_address(consumed).value()};
}

TEST_CASE("uri")
{
	SECTION("uri_view: success")
	{
		const auto &[text, expected, canonical_text] = GENERATE(from_range(std::span{parse_success}));
		CAPTURE(text);
		const auto canonical = canonical_text.data() != nullptr ? canonical_text : text;

		auto uri = check_make<pal::uri_view>(text);
		REQUIRE(uri);
		CHECK(*uri == expected);

		CHECK(check_to_chars(expected) == canonical);
		auto reparsed = pal::uri_view::make(canonical);
		REQUIRE(reparsed);
		CHECK(*reparsed == expected);
	}

	SECTION("uri_view: parse failure")
	{
		const auto &text = GENERATE(from_range(std::span{parse_failure}));
		CAPTURE(text);
		CHECK_FALSE(check_make<pal::uri_view>(text));
	}

	SECTION("uri_view: format failure")
	{
		check_rejects(GENERATE(from_range(std::span{format_failure})));
	}

	SECTION("uri_host: success")
	{
		const auto &[text, ip, rest] = GENERATE(from_range(std::span{host_success}));
		CAPTURE(text);

		pal::uri_host host;
		auto [ptr, ec] = host.from_chars(text.data(), text.data() + text.size());
		REQUIRE(ec == std::errc{});
		const auto consumed = static_cast<size_t>(ptr - text.data());
		CHECK(text.substr(consumed) == rest);
		CHECK(host == expected_host(text.substr(0, consumed), ip));
		CHECK(host.is_valid());
		CHECK(check_make<pal::uri_host>(text).has_value() == rest.empty());

		const auto formatted = check_to_chars(host);
		auto reparsed = pal::uri_host::make(formatted);
		REQUIRE(reparsed);
		CHECK(*reparsed == host);
	}

	SECTION("uri_host: parse failure")
	{
		const auto &text = GENERATE(from_range(std::span{host_failure}));
		CAPTURE(text);
		CHECK_FALSE(check_make<pal::uri_host>(text));
	}

	SECTION("uri_host: format success")
	{
		const auto &[host, expected] = GENERATE(from_range(std::span{host_format_success}));
		CAPTURE(expected);
		CHECK(host.is_valid());
		CHECK(check_to_chars(host) == expected);
	}

	SECTION("uri_host: format failure")
	{
		const auto &host = GENERATE(from_range(std::span{host_format_failure}));
		CHECK_FALSE(host.is_valid());
		check_rejects(host);
	}
}

} // namespace
