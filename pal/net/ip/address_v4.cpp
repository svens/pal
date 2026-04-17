#include <pal/net/ip/address_v4.hpp>
#include <algorithm>

#if __has_include(<arpa/inet.h>)
	#include <arpa/inet.h>
#elif __has_include(<ws2tcpip.h>)
	#include <ws2tcpip.h>
#endif

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

std::from_chars_result address_v4::from_chars (const char *first, const char *last) noexcept
{
	auto len = static_cast<size_t>(last - first);
	len = (std::min)(len, max_string_length);

	std::array<char, max_string_length + 1> buf{};
	std::copy_n(first, len, buf.data());

	if (::inet_pton(AF_INET, buf.data(), bytes_.data()) == 1)
	{
		return {.ptr = first + len, .ec = std::errc{}};
	}
	return {.ptr = first, .ec = std::errc::invalid_argument};
}

} // namespace pal::net::ip
