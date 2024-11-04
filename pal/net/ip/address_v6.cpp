#include <pal/net/ip/address_v6>

#if __has_include(<arpa/inet.h>)
	#include <arpa/inet.h>
#elif __has_include(<ws2tcpip.h>)
	#include <ws2tcpip.h>
#endif

namespace pal::net::ip {

namespace __address_v6 {

char *ntop (const uint8_t *bytes, char *first, char *last) noexcept
{
	if (::inet_ntop(AF_INET6, bytes, first, last - first))
	{
		while (*first)
		{
			first++;
		}
		return first;
	}
	return nullptr;
}

} // namespace __address_v6

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
