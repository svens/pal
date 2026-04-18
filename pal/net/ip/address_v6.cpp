#include <pal/net/ip/address_v6.hpp>
#include <algorithm>

#if __has_include(<arpa/inet.h>)
	#include <arpa/inet.h>
#elif __has_include(<ws2tcpip.h>)
	#include <ws2tcpip.h>
#endif

namespace pal::net::ip
{

namespace __address_v6
{

char *ntop (const uint8_t *bytes, char *first, char *last) noexcept
{
	if (::inet_ntop(AF_INET6, bytes, first, last - first) != nullptr)
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
	if (auto *p = __address_v6::ntop(bytes_.data(), first, last))
	{
		if (scope_id_ == 0)
		{
			return {.ptr = p, .ec = std::errc{}};
		}
		if (p < last)
		{
			if (auto [end, ec] = std::to_chars(p + 1, last, scope_id_); ec == std::errc{})
			{
				*p = '%';
				return {.ptr = end, .ec = std::errc{}};
			}
		}
	}
	return {.ptr = last, .ec = std::errc::value_too_large};
}

std::from_chars_result address_v6::from_chars (const char *first, const char *last) noexcept
{
	last = (std::min)(first + max_string_length, last);

	std::array<char, max_string_length + 1> buf{};
	auto *buf_end = std::copy(first, last, buf.data());

	scope_id_ = 0;
	if (auto *p = std::find(buf.data(), buf_end, '%'); p != buf_end)
	{
		auto [ptr, ec] = std::from_chars(p + 1, buf_end, scope_id_);
		if (ec != std::errc{} || ptr != buf_end)
		{
			return {.ptr = first, .ec = std::errc::invalid_argument};
		}
		*p = '\0';
	}

	if (::inet_pton(AF_INET6, buf.data(), bytes_.data()) == 1)
	{
		return {.ptr = last, .ec = std::errc{}};
	}

	return {.ptr = first, .ec = std::errc::invalid_argument};
}

} // namespace pal::net::ip
