#include <pal/net/ip/address_v6.hpp>

#if __has_include(<arpa/inet.h>)
	#include <arpa/inet.h>
#elif __has_include(<ws2tcpip.h>)
	#include <ws2tcpip.h>
#endif

namespace
{

constexpr bool is_v6_char (char c) noexcept
{
	return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F') || c == ':' || c == '.';
}

} // namespace

namespace pal::net::ip
{

namespace __address_v6
{

char *ntop (const uint8_t *bytes, char *first, char *last) noexcept
{
	if (first < last && ::inet_ntop(AF_INET6, bytes, first, last - first) != nullptr)
	{
		while (*first != '\0')
		{
			first++;
		}
		return first;
	}
	return nullptr;
}

} // namespace __address_v6

std::to_chars_result address_v6::to_chars (char *first, char *last) const noexcept
{
	std::to_chars_result r{.ptr = last, .ec = std::errc::value_too_large};
	if (auto *p = __address_v6::ntop(bytes_.data(), first, last))
	{
		if (scope_id_ == 0)
		{
			r = {.ptr = p, .ec = std::errc{}};
		}
		// p points to the '\0' written by ntop
		// ntop only succeeds when the buffer fits the full string including '\0', so p < last is guaranteed
		else if (r = std::to_chars(p + 1, last, scope_id_); r.ec == std::errc{})
		{
			*p = '%';
		}
	}
	return r;
}

std::from_chars_result address_v6::from_chars (const char *first, const char *last) noexcept
{
	std::from_chars_result r{.ptr = first, .ec = std::errc{}};
	last = first + (std::min<ptrdiff_t>)(last - first, max_string_length);

	std::array<char, max_string_length + 1> buf{};
	auto *buf_p = buf.data();

	while (r.ptr != last && is_v6_char(*r.ptr))
	{
		*buf_p++ = *r.ptr++;
	}

	if (::inet_pton(AF_INET6, buf.data(), bytes_.data()) != 1)
	{
		r.ec = std::errc::invalid_argument;
		return r;
	}

	scope_id_ = 0;
	if (r.ptr != last && *r.ptr == '%')
	{
		r = std::from_chars(r.ptr + 1, last, scope_id_);
		if (r.ec != std::errc{})
		{
			return r;
		}
	}

	return r;
}

} // namespace pal::net::ip
