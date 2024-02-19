#include <pal/conv>
#include <pal/byte_order>
#include <array>

namespace pal {

namespace {

constexpr const auto bad = std::byte{0xff};

constexpr auto make_encode_map () noexcept
{
	std::array<uint16_t, 256> result{};
	for (auto v = 0u;  v < result.size();  ++v)
	{
		constexpr char digits[] = "0123456789abcdef";
		result[v] = hton(static_cast<uint16_t>((digits[v >> 4] << 8) | digits[v & 0x0f]));
	}
	return result;
}

constexpr auto make_decode_map () noexcept
{
	std::array<std::byte, 256> result{};
	for (auto v = 0u;  v < result.size();  ++v)
	{
		if ('0' <= v && v <= '9')
		{
			result[v] = std::byte{static_cast<uint8_t>(v - '0')};
		}
		else if ('a' <= v && v <= 'f')
		{
			result[v] = std::byte{static_cast<uint8_t>(v - 'a' + 10)};
		}
		else if ('A' <= v && v <= 'F')
		{
			result[v] = std::byte{static_cast<uint8_t>(v - 'A' + 10)};
		}
		else
		{
			result[v] = bad;
		}
	}
	return result;
}

} // namespace

std::byte *hex::encode (const std::byte *first, const std::byte *last, std::byte *out) noexcept
{
	static constexpr const auto map = make_encode_map();

	auto it = reinterpret_cast<uint16_t *>(out);
	while (first != last)
	{
		*it++ = map[std::to_integer<size_t>(*first++)];
	}

	return reinterpret_cast<std::byte *>(it);
}

std::byte *hex::decode (const std::byte *first, const std::byte *last, std::byte *out) noexcept
{
	static constexpr const auto map = make_decode_map();

	if ((last - first) % 2 != 0)
	{
		return nullptr;
	}

	for (/**/;  first != last;  first += 2)
	{
		auto hi = map[std::to_integer<uint8_t>(first[0])];
		auto lo = map[std::to_integer<uint8_t>(first[1])];
		if (hi != bad && lo != bad)
		{
			*out++ = (hi << 4) | lo;
		}
		else
		{
			return nullptr;
		}
	}

	return out;
}

} // namespace pal
