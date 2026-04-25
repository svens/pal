#include <pal/net/ip/network_v4.hpp>

namespace pal::net::ip
{

std::to_chars_result network_v4::to_chars (char *first, char *last) const noexcept
{
	auto r = address_.to_chars(first, last);
	if (r.ec == std::errc{})
	{
		*r.ptr++ = '/';
		r = std::to_chars(r.ptr, last, prefix_length_);
	}
	return r;
}

std::from_chars_result network_v4::from_chars (const char *first, const char *last) noexcept
{
	address_v4 addr;
	auto r = addr.from_chars(first, last);
	if (r.ec != std::errc{})
	{
		return r;
	}

	prefix_length_ = max_prefix_length;
	if (r.ptr != last && *r.ptr == '/')
	{
		r = std::from_chars(r.ptr + 1, last, prefix_length_);
		if (r.ec != std::errc{} || prefix_length_ > max_prefix_length)
		{
			r.ec = std::errc::invalid_argument;
			return r;
		}
	}

	address_ = address_v4{addr.to_uint() & __network_v4::make_mask(prefix_length_)};
	return r;
}

} // namespace pal::net::ip
