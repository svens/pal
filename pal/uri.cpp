#include <pal/uri.hpp>
#include <algorithm>
#include <array>
#include <cstdint>
#include <tuple>

namespace pal
{

namespace
{

// LCOV_EXCL_START

enum: uint16_t
{
	userinfo_char = 1 << 0,
	reg_name_char = 1 << 1,
	query_fragment_char = 1 << 2,
	path_char = 1 << 3,
	scheme_char = 1 << 4,
	hex_char = 1 << 5,
	digit_char = 1 << 6,
	alpha_char = 1 << 7,
};

constexpr void add (std::array<uint16_t, 256> &map, std::string_view chars, uint16_t bits) noexcept
{
	for (auto c: chars)
	{
		map[static_cast<uint8_t>(c)] = static_cast<uint16_t>(map[static_cast<uint8_t>(c)] | bits);
	}
}

constexpr auto make_char_map () noexcept
{
	constexpr std::string_view alpha = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
	constexpr std::string_view digit = "0123456789";
	constexpr std::string_view hex_alpha = "ABCDEFabcdef";
	constexpr std::string_view unreserved_punct = "-._~";
	constexpr std::string_view sub_delims = "!$&'()*+,;=";

	constexpr uint16_t component_chars = userinfo_char
		| reg_name_char
		| query_fragment_char
		| path_char
	;

	std::array<uint16_t, 256> map{};
	add(map, alpha, component_chars | scheme_char | alpha_char);
	add(map, digit, component_chars | scheme_char | hex_char | digit_char);
	add(map, hex_alpha, hex_char);
	add(map, unreserved_punct, component_chars);
	add(map, sub_delims, component_chars);
	add(map, ":", userinfo_char | query_fragment_char | path_char);
	add(map, "@", query_fragment_char | path_char);
	add(map, "/", query_fragment_char | path_char);
	add(map, "?", query_fragment_char);
	add(map, "+-.", scheme_char);
	return map;
}

// LCOV_EXCL_STOP

constexpr auto char_map = make_char_map();

constexpr bool is (char c, uint16_t bits) noexcept
{
	return (char_map[static_cast<uint8_t>(c)] & bits) != 0;
}

constexpr std::string_view head (std::string_view text, size_t n) noexcept
{
	text.remove_suffix(text.size() - (std::min)(n, text.size()));
	return text;
}

constexpr std::string_view tail (std::string_view text, size_t pos) noexcept
{
	text.remove_prefix(pos);
	return text;
}

std::optional<size_t> prefix_size (std::string_view text, uint16_t bits) noexcept
{
	size_t i = 0;
	while (i < text.size())
	{
		if (text[i] == '%')
		{
			if (i + 2 >= text.size() || !is(text[i + 1], hex_char) || !is(text[i + 2], hex_char))
			{
				return std::nullopt;
			}
			i += 3;
		}
		else if (is(text[i], bits))
		{
			++i;
		}
		else
		{
			break;
		}
	}
	return i;
}

bool matches_class (std::string_view text, uint16_t bits) noexcept
{
	return prefix_size(text, bits) == text.size();
}

bool is_scheme (std::string_view text) noexcept
{
	return !text.empty()
		&& is(text[0], alpha_char)
		&& std::ranges::all_of(text, [](char c) { return is(c, scheme_char); })
	;
}

bool is_dec_octet (std::string_view text) noexcept
{
	if (text.empty() || text.size() > 3 || (text.size() > 1 && text[0] == '0'))
	{
		return false;
	}
	unsigned value = 0;
	for (auto c: text)
	{
		if (!is(c, digit_char))
		{
			return false;
		}
		value = value * 10 + static_cast<unsigned>(c - '0');
	}
	return value <= 255;
}

bool is_ipv4 (std::string_view text) noexcept
{
	for (auto i = 0; i < 3; ++i)
	{
		auto dot = text.find('.');
		if (dot == text.npos || !is_dec_octet(head(text, dot)))
		{
			return false;
		}
		text.remove_prefix(dot + 1);
	}
	return is_dec_octet(text);
}

bool is_h16 (std::string_view text) noexcept
{
	return !text.empty()
		&& text.size() <= 4
		&& std::ranges::all_of(text, [](char c) { return is(c, hex_char); })
	;
}

std::optional<size_t> ipv6_groups (std::string_view text, bool allow_ipv4) noexcept
{
	size_t groups = 0;
	if (text.empty())
	{
		return groups;
	}
	for (auto colon = text.find(':'); colon != text.npos; colon = text.find(':'))
	{
		if (!is_h16(head(text, colon)))
		{
			return std::nullopt;
		}
		++groups;
		text.remove_prefix(colon + 1);
	}
	if (allow_ipv4 && is_ipv4(text))
	{
		return groups + 2;
	}
	if (!is_h16(text))
	{
		return std::nullopt;
	}
	return groups + 1;
}

bool is_ipv6 (std::string_view text) noexcept
{
	auto gap = text.find("::");
	if (gap == text.npos)
	{
		return ipv6_groups(text, true) == 8;
	}
	auto before = ipv6_groups(head(text, gap), false);
	auto after = ipv6_groups(tail(text, gap + 2), true);
	return before && after && *before + *after <= 7;
}

net::ip::address to_address (std::string_view text) noexcept
{
	net::ip::address address;
	std::ignore = address.from_chars(text.data(), text.data() + text.size());
	return address;
}

struct host_scan
{
	uri_host host;
	size_t size;
};

std::optional<host_scan> scan_host (std::string_view text) noexcept
{
	if (text.starts_with('['))
	{
		auto close = text.find(']');
		if (close == text.npos)
		{
			return std::nullopt;
		}
		auto address = head(tail(text, 1), close - 1);
		if (!is_ipv6(address))
		{
			return std::nullopt;
		}
		return host_scan{.host = {.value = to_address(address)}, .size = close + 1};
	}

	auto size = prefix_size(text, reg_name_char);
	if (!size)
	{
		return std::nullopt;
	}
	auto host = head(text, *size);
	if (is_ipv4(host))
	{
		return host_scan{.host = {.value = to_address(host)}, .size = *size};
	}
	return host_scan{.host = {.value = host}, .size = *size};
}

std::optional<net::ip::port_type> parse_port (std::string_view text) noexcept
{
	uint16_t value = 0;
	auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
	if (ec != std::errc{} || ptr != text.data() + text.size())
	{
		return std::nullopt;
	}
	return net::ip::port_type{value};
}

constexpr size_t port_max_size = sizeof(":65535") - 1;

struct writer
{
	char *ptr;
	char *last;

	bool put_text (std::string_view text) noexcept
	{
		if (static_cast<size_t>(last - ptr) < text.size())
		{
			return false;
		}
		ptr = std::ranges::copy(text, ptr).out;
		return true;
	}

	bool put_port (net::ip::port_type port) noexcept
	{
		if (!put_text(":"))
		{
			return false;
		}
		auto [end, ec] = std::to_chars(ptr, last, static_cast<uint16_t>(port));
		ptr = end;
		return ec == std::errc{};
	}
};

bool is_valid (const uri_view &uri) noexcept
{
	if (uri.scheme && !is_scheme(*uri.scheme))
	{
		return false;
	}
	if (uri.host)
	{
		if (!uri_host::make(*uri.host))
		{
			return false;
		}
		if (uri.userinfo && !matches_class(*uri.userinfo, userinfo_char))
		{
			return false;
		}
		if (!uri.path.empty() && uri.path[0] != '/')
		{
			return false;
		}
	}
	else
	{
		if (uri.userinfo || uri.port || uri.path.starts_with("//"))
		{
			return false;
		}
		if (!uri.scheme && head(uri.path, uri.path.find('/')).contains(':'))
		{
			return false;
		}
	}
	return matches_class(uri.path, path_char)
		&& (!uri.query || matches_class(*uri.query, query_fragment_char))
		&& (!uri.fragment || matches_class(*uri.fragment, query_fragment_char))
	;
}

std::from_chars_result invalid_input (const char *first) noexcept
{
	return {.ptr = first, .ec = std::errc::invalid_argument};
}

std::to_chars_result fail (char *last, std::errc ec) noexcept
{
	return {.ptr = last, .ec = ec};
}

} // namespace

result<uri_host> uri_host::make (std::string_view text) noexcept
{
	uri_host host;
	auto [ptr, ec] = host.from_chars(text.data(), text.data() + text.size());
	if (ec != std::errc{} || ptr != text.data() + text.size())
	{
		return make_unexpected(std::errc::invalid_argument);
	}
	return host;
}

std::from_chars_result uri_host::from_chars (const char *first, const char *last) noexcept
{
	auto scan = scan_host({first, last});
	if (!scan)
	{
		return invalid_input(first);
	}
	*this = scan->host;
	return {.ptr = first + scan->size, .ec = {}};
}

std::to_chars_result uri_host::to_chars (char *first, char *last) const noexcept
{
	if (!is_valid())
	{
		return fail(last, std::errc::invalid_argument);
	}

	writer out{.ptr = first, .last = last};

	if (const auto *reg_name = std::get_if<std::string_view>(&value))
	{
		if (!out.put_text(*reg_name))
		{
			return fail(last, std::errc::value_too_large);
		}
		return {.ptr = out.ptr, .ec = {}};
	}

	const auto &address = *std::get_if<net::ip::address>(&value);
	std::array<char, net::ip::address::max_string_length + 1> buf{};
	const auto text = std::string_view{buf.data(), address.to_chars(buf.data(), buf.data() + buf.size()).ptr};
	const auto ok = address.is_v4()
		? out.put_text(text)
		: out.put_text("[") && out.put_text(text) && out.put_text("]")
	;
	if (!ok)
	{
		return fail(last, std::errc::value_too_large);
	}
	return {.ptr = out.ptr, .ec = {}};
}

bool uri_host::is_valid () const noexcept
{
	if (const auto *reg_name = std::get_if<std::string_view>(&value))
	{
		return !is_ipv4(*reg_name) && matches_class(*reg_name, reg_name_char);
	}
	const auto *v6 = std::get_if<net::ip::address>(&value)->v6();
	return v6 == nullptr || v6->scope_id() == 0;
}

size_t uri_host::max_size () const noexcept
{
	if (const auto *reg_name = std::get_if<std::string_view>(&value))
	{
		return reg_name->size();
	}
	if (std::get_if<net::ip::address>(&value)->is_v4())
	{
		return net::ip::address_v4::max_string_length;
	}
	return net::ip::address_v6::max_string_length + sizeof("[]") - 1;
}

result<uri_view> uri_view::make (std::string_view text) noexcept
{
	uri_view uri;
	if (uri.from_chars(text.data(), text.data() + text.size()).ec != std::errc{})
	{
		return make_unexpected(std::errc::invalid_argument);
	}
	return uri;
}

std::from_chars_result uri_view::from_chars (const char *first, const char *last) noexcept
{
	std::string_view text{first, last};
	*this = {};

	if (auto hash = text.find('#'); hash != text.npos)
	{
		fragment = tail(text, hash + 1);
		if (!matches_class(*fragment, query_fragment_char))
		{
			return invalid_input(first);
		}
		text = head(text, hash);
	}

	if (auto question = text.find('?'); question != text.npos)
	{
		query = tail(text, question + 1);
		if (!matches_class(*query, query_fragment_char))
		{
			return invalid_input(first);
		}
		text = head(text, question);
	}

	if (auto colon = text.find_first_of(":/"); colon != text.npos && text[colon] == ':')
	{
		scheme = head(text, colon);
		if (!is_scheme(*scheme))
		{
			return invalid_input(first);
		}
		text.remove_prefix(colon + 1);
	}

	if (text.starts_with("//"))
	{
		text.remove_prefix(2);
		auto authority = head(text, text.find('/'));
		text.remove_prefix(authority.size());

		if (auto at = authority.find('@'); at != authority.npos)
		{
			userinfo = head(authority, at);
			if (!matches_class(*userinfo, userinfo_char))
			{
				return invalid_input(first);
			}
			authority.remove_prefix(at + 1);
		}

		auto scan = scan_host(authority);
		if (!scan)
		{
			return invalid_input(first);
		}
		host = head(authority, scan->size);
		authority.remove_prefix(scan->size);

		if (!authority.empty())
		{
			if (authority[0] != ':')
			{
				return invalid_input(first);
			}
			auto port_text = tail(authority, 1);
			port = parse_port(port_text);
			if (!port_text.empty() && !port)
			{
				return invalid_input(first);
			}
		}
	}

	path = text;
	if (!matches_class(path, path_char))
	{
		return invalid_input(first);
	}
	return {.ptr = last, .ec = {}};
}

std::to_chars_result uri_view::to_chars (char *first, char *last) const noexcept
{
	if (!is_valid(*this))
	{
		return fail(last, std::errc::invalid_argument);
	}

	writer out{.ptr = first, .last = last};
	auto ok = !scheme || (out.put_text(*scheme) && out.put_text(":"));
	if (ok && host)
	{
		ok = out.put_text("//")
			&& (!userinfo || (out.put_text(*userinfo) && out.put_text("@")))
			&& out.put_text(*host)
			&& (!port || out.put_port(*port))
		;
	}
	ok = ok
		&& out.put_text(path)
		&& (!query || (out.put_text("?") && out.put_text(*query)))
		&& (!fragment || (out.put_text("#") && out.put_text(*fragment)))
	;

	if (!ok)
	{
		return fail(last, std::errc::value_too_large);
	}
	return {.ptr = out.ptr, .ec = {}};
}

size_t uri_view::max_size () const noexcept
{
	auto size = path.size();
	if (scheme)
	{
		size += scheme->size() + 1;
	}
	if (userinfo)
	{
		size += userinfo->size() + 1;
	}
	if (host)
	{
		size += 2 + host->size();
	}
	if (port)
	{
		size += port_max_size;
	}
	if (query)
	{
		size += 1 + query->size();
	}
	if (fragment)
	{
		size += 1 + fragment->size();
	}
	return size;
}

} // namespace pal
