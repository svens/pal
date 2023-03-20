#include <pal/net/ip/address_v6>

#if __has_include(<arpa/inet.h>)
	#include <arpa/inet.h>
#elif __has_include(<ws2tcpip.h>)
	#include <ws2tcpip.h>
#endif

namespace pal::net::ip {

std::to_chars_result address_v6::to_chars (char *first, char *last) const noexcept
{
	if (::inet_ntop(AF_INET6, bytes_.data(), first, last - first))
	{
		while (*first)
		{
			first++;
		}
		return {first, std::errc{}};
	}
	return {last, std::errc::value_too_large};
}

std::from_chars_result address_v6::from_chars (const char *first, const char *last) noexcept
{
	if (first + max_string_length < last)
	{
		last = first + max_string_length;
	}

	auto ptr = first;
	char buf[max_string_length + 1] = {};
	if (*last != '\0')
	{
		auto dest = buf;
		while (ptr < last && *ptr != '\0')
		{
			*dest++ = *ptr++;
		}
		last = std::exchange(ptr, buf);
	}

	if (::inet_pton(AF_INET6, ptr, bytes_.data()) == 1)
	{
		return {last, std::errc{}};
	}

	return {first, std::errc::invalid_argument};
}

} // namespace pal::net::ip
