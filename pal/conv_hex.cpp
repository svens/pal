#include <pal/conv>
#include <pal/byte_order>
#include <array>


__pal_begin


namespace __bits {


namespace {


constexpr auto generate_to_hex_map () noexcept
{
	std::array<uint16_t, 256> result{};
	for (auto v = 0u;  v < result.size();  ++v)
	{
		constexpr char digits[] = "0123456789abcdef";
		result[v] = hton(static_cast<uint16_t>((digits[v >> 4] << 8) | digits[v & 0x0f]));
	}
	return result;
}


constexpr auto generate_from_hex_map () noexcept
{
	std::array<uint8_t, 256> result{};
	for (auto v = 0u;  v < result.size();  ++v)
	{
		if ('0' <= v && v <= '9')
		{
			result[v] = static_cast<uint8_t>(v - '0');
		}
		else if ('a' <= v && v <= 'f')
		{
			result[v] = static_cast<uint8_t>(v - 'a' + 10);
		}
		else if ('A' <= v && v <= 'F')
		{
			result[v] = static_cast<uint8_t>(v - 'A' + 10);
		}
		else
		{
			result[v] = 0xff;
		}
	}
	return result;
}


} // namespace


char *to_hex (const uint8_t *first, const uint8_t *last, char *out) noexcept
{
	static constexpr auto map = generate_to_hex_map();
	auto it = reinterpret_cast<uint16_t *>(out);
	while (first != last)
	{
		*it++ = map[*first++];
	}
	return reinterpret_cast<char *>(it);
}


char *from_hex (const uint8_t *first, const uint8_t *last, char *out) noexcept
{
	static constexpr auto map = generate_from_hex_map();

	if ((last - first) % 2 != 0)
	{
		return nullptr;
	}

	uint8_t hi, lo;
	while (first != last)
	{
		hi = map[first[0]];
		lo = map[first[1]];
		if (hi != 0xff && lo != 0xff)
		{
			*out++ = (hi << 4) | lo;
			first += 2;
		}
		else
		{
			return nullptr;
		}
	}

	return out;
}


} // namespace __bits


__pal_end
