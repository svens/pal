#include <pal/codec.hpp>
#include <array>
#include <string_view>

namespace pal
{

namespace
{

// NOLINTBEGIN(readability-magic-numbers)
// LCOV_EXCL_START

constexpr std::string_view digits = "0123456789ABCDEF";
constexpr uint8_t invalid = 0xff;

constexpr auto make_unreserved_map () noexcept
{
	constexpr std::string_view unreserved =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
		"abcdefghijklmnopqrstuvwxyz"
		"0123456789"
		"-._~"
	;

	std::array<bool, 256> result{};
	for (auto c: unreserved)
	{
		result[static_cast<uint8_t>(c)] = true;
	}
	return result;
}

constexpr auto make_decode_map () noexcept
{
	std::array<uint8_t, 256> result{};
	result.fill(invalid);

	for (uint32_t v = '0'; v <= '9'; ++v)
	{
		result[v] = static_cast<uint8_t>(v - '0');
	}

	for (uint32_t v = 'a'; v <= 'f'; ++v)
	{
		result[v] = static_cast<uint8_t>(v - 'a' + 10);
	}

	for (uint32_t v = 'A'; v <= 'F'; ++v)
	{
		result[v] = static_cast<uint8_t>(v - 'A' + 10);
	}

	return result;
}

// LCOV_EXCL_STOP
// NOLINTEND(readability-magic-numbers)

} // namespace

convert_result<std::byte *>
percent_encoder::convert (const std::byte *first, const std::byte *last, std::byte *out, std::byte *out_last) noexcept
{
	static constexpr auto unreserved = make_unreserved_map();

	if (static_cast<size_t>(out_last - out) < max_size(static_cast<size_t>(last - first)))
	{
		return {.ptr = nullptr, .ec = std::errc::value_too_large};
	}

	while (first != last)
	{
		const auto c = std::to_integer<uint8_t>(*first++);
		if (unreserved[c])
		{
			*out++ = std::byte{c};
		}
		else
		{
			*out++ = std::byte{'%'};
			*out++ = std::byte{static_cast<uint8_t>(digits[c >> 4])};
			*out++ = std::byte{static_cast<uint8_t>(digits[c & 0x0f])};
		}
	}

	return {.ptr = out, .ec = {}};
}

convert_result<std::byte *>
percent_decoder::convert (const std::byte *first, const std::byte *last, std::byte *out, std::byte *out_last) noexcept
{
	static constexpr auto map = make_decode_map();

	if (static_cast<size_t>(out_last - out) < max_size(static_cast<size_t>(last - first)))
	{
		return {.ptr = nullptr, .ec = std::errc::value_too_large};
	}

	while (first != last)
	{
		const auto c = *first++;
		if (c != std::byte{'%'})
		{
			*out++ = c;
			continue;
		}

		if (last - first < 2)
		{
			return {.ptr = nullptr, .ec = std::errc::illegal_byte_sequence};
		}

		auto hi = map[std::to_integer<uint8_t>(first[0])];
		auto lo = map[std::to_integer<uint8_t>(first[1])];
		if (hi == invalid || lo == invalid)
		{
			return {.ptr = nullptr, .ec = std::errc::illegal_byte_sequence};
		}

		*out++ = std::byte{static_cast<uint8_t>((hi << 4) | lo)};
		first += 2;
	}

	return {.ptr = out, .ec = {}};
}

} // namespace pal
