#include <pal/net/ip/address_v4.hpp>
#include <algorithm>

#if __has_include(<arpa/inet.h>)
	#include <arpa/inet.h>
#elif __has_include(<ws2tcpip.h>)
	#include <ws2tcpip.h>
#endif

namespace
{

constexpr bool is_v4_char (char c) noexcept
{
	return c == '.' || (c >= '0' && c <= '9');
}

} // namespace

namespace pal::net::ip
{

namespace __address_v4
{

char *ntop (const uint8_t *bytes, char *first, char *last) noexcept
{
	if (::inet_ntop(AF_INET, bytes, first, static_cast<size_t>(last - first)) != nullptr)
	{
		while (*first != '\0')
		{
			first++;
		}
		return first;
	}
	return nullptr;
}

} // namespace __address_v4

std::to_chars_result address_v4::to_chars (char *first, char *last) const noexcept
{
	std::to_chars_result r{.ptr = last, .ec = std::errc::value_too_large};
	if (auto *p = __address_v4::ntop(bytes_.data(), first, last))
	{
		r = {.ptr = p, .ec = std::errc{}};
	}
	return r;
}

std::from_chars_result address_v4::from_chars (const char *first, const char *last) noexcept
{
	std::from_chars_result r{.ptr = first, .ec = std::errc{}};
	last = first + (std::min<ptrdiff_t>)(last - first, max_string_length);

	std::array<char, max_string_length + 1> buf{};
	auto *buf_p = buf.data();

	while (r.ptr != last && is_v4_char(*r.ptr))
	{
		*buf_p++ = *r.ptr++;
	}

	if (::inet_pton(AF_INET, buf.data(), bytes_.data()) != 1)
	{
		r.ec = std::errc::invalid_argument;
	}

	return r;
}

} // namespace pal::net::ip
