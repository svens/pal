#include <pal/net/ip/basic_endpoint.hpp>

namespace pal::net::ip::__endpoint
{

namespace
{

char *add_port (port_type port, char *first, char *last) noexcept
{
	// guard required: first + 1 is evaluated before std::to_chars can check it
	if (first < last)
	{
		if (auto [p, ec] = std::to_chars(first + 1, last, ntoh(port)); ec == std::errc{})
		{
			*first = ':';
			return p;
		}
	}
	return nullptr;
}

} // namespace

char *ntop (const v4 &a, char *first, char *last) noexcept
{
	const auto *bytes = reinterpret_cast<const uint8_t *>(&a.sin_addr);
	if (first = __address_v4::ntop(bytes, first, last); first != nullptr)
	{
		return add_port(a.sin_port, first, last);
	}
	return nullptr;
}

char *ntop (const v6 &a, char *first, char *last) noexcept
{
	// guard required: first + 1 is evaluated before __address_v6::ntop can check it
	if (first < last)
	{
		const auto *bytes = reinterpret_cast<const uint8_t *>(&a.sin6_addr);
		if (auto *end = __address_v6::ntop(bytes, first + 1, last); end != nullptr)
		{
			*first = '[';
			*end++ = ']';
			return add_port(a.sin6_port, end, last);
		}
	}
	return nullptr;
}

} // namespace pal::net::ip::__endpoint
