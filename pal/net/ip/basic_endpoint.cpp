#include <pal/net/ip/basic_endpoint>

#if __pal_net_posix
	#include <arpa/inet.h>
#elif __pal_net_winsock
	#include <ws2tcpip.h>
#endif

namespace pal::net::ip::__endpoint {

namespace {

char *add_port (port_type port, char *first, char *last) noexcept
{
	if (first + sizeof(":1") - 1 < last)
	{
		*first++ = ':';
		auto [p, ec] = std::to_chars(first, last, ntoh(port));
		if (ec == std::errc{})
		{
			return p;
		}
	}
	return nullptr;
}

} // namespace

char *ntop (const v4 &a, char *first, char *last) noexcept
{
	auto bytes = reinterpret_cast<const uint8_t *>(&a.sin_addr);
	if (first = __address_v4::ntop(bytes, first, last); first)
	{
		return add_port(a.sin_port, first, last);
	}
	return nullptr;
}

char *ntop (const v6 &a, char *first, char *last) noexcept
{
	if (first + sizeof("[::]:1") - 1 < last)
	{
		*first++ = '[';
		auto bytes = reinterpret_cast<const uint8_t *>(&a.sin6_addr);
		if (first = __address_v6::ntop(bytes, first, last); first)
		{
			if (first + sizeof("]:1") - 1 < last)
			{
				*first++ = ']';
				return add_port(a.sin6_port, first, last);
			}
		}
	}
	return nullptr;
}

} // namespace pal::net::ip::__endpoint
