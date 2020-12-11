#include <pal/conv>
#include <pal/byte_order>
#include <array>


__pal_begin


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


conv_result to_hex (const char *first, const char *last, char *out) noexcept
{
	static constexpr auto map = generate_to_hex_map();
	auto it = reinterpret_cast<uint16_t *>(out);
	while (first != last)
	{
		*it++ = map[uint8_t(*first++)];
	}
	return {first, reinterpret_cast<char *>(it)};
}


conv_result from_hex (const char *first, const char *last, char *out) noexcept
{
	static constexpr auto map = generate_from_hex_map();
	for (/**/;  first + 1 < last;  first += 2)
	{
		auto hi = map[first[0]], lo = map[first[1]];
		if (hi != 0xff && lo != 0xff)
		{
			*out++ = (hi << 4) | lo;
		}
		else
		{
			if (hi != 0xff)
			{
				first++;
			}
			break;
		}
	}
	return {first, out};
}


__pal_end
