#include <pal/codec.hpp>
#include <array>
#include <cstdint>
#include <string_view>

namespace pal
{

// NOLINTBEGIN(readability-magic-numbers)

namespace
{

constexpr uint8_t invalid = 0xff;

// LCOV_EXCL_START
constexpr auto make_decode_map () noexcept
{
	std::array<uint8_t, 256> result{};
	result.fill(invalid);

	for (uint32_t v = 'A'; v <= 'Z'; ++v)
	{
		result[v] = static_cast<uint8_t>(v - 'A');
	}

	for (uint32_t v = 'a'; v <= 'z'; ++v)
	{
		result[v] = static_cast<uint8_t>(v - 'a' + 26);
	}

	for (uint32_t v = '0'; v <= '9'; ++v)
	{
		result[v] = static_cast<uint8_t>(v - '0' + 52);
	}

	result['+'] = 62;
	result['/'] = 63;

	return result;
}
// LCOV_EXCL_STOP

} // namespace

convert_result<std::byte *>
base64_encoder::convert (const std::byte *first, const std::byte *last, std::byte *out, std::byte *out_last) noexcept
{
	// clang-format off

	static constexpr uint8_t mask = 0b00111111;
	static constexpr std::string_view map =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
		"abcdefghijklmnopqrstuvwxyz"
		"0123456789+/";

	// clang-format on

	auto n = static_cast<size_t>(last - first);
	if (static_cast<size_t>(out_last - out) < max_size(n))
	{
		return {.ptr = nullptr, .ec = std::errc::value_too_large};
	}

	while (last - first >= 3)
	{
		auto a = std::to_integer<uint8_t>(first[0]);
		auto b = std::to_integer<uint8_t>(first[1]);
		auto c = std::to_integer<uint8_t>(first[2]);
		*out++ = std::byte{static_cast<uint8_t>(map[a >> 2])};
		*out++ = std::byte{static_cast<uint8_t>(map[((a << 4) | (b >> 4)) & mask])};
		*out++ = std::byte{static_cast<uint8_t>(map[((b << 2) | (c >> 6)) & mask])};
		*out++ = std::byte{static_cast<uint8_t>(map[c & mask])};
		first += 3;
	}

	if (last - first == 1)
	{
		auto a = std::to_integer<uint8_t>(first[0]);
		*out++ = std::byte{static_cast<uint8_t>(map[a >> 2])};
		*out++ = std::byte{static_cast<uint8_t>(map[(a << 4) & mask])};
		*out++ = std::byte{'='};
		*out++ = std::byte{'='};
	}
	else if (last - first == 2)
	{
		auto a = std::to_integer<uint8_t>(first[0]);
		auto b = std::to_integer<uint8_t>(first[1]);
		*out++ = std::byte{static_cast<uint8_t>(map[a >> 2])};
		*out++ = std::byte{static_cast<uint8_t>(map[((a << 4) | (b >> 4)) & mask])};
		*out++ = std::byte{static_cast<uint8_t>(map[(b << 2) & mask])};
		*out++ = std::byte{'='};
	}

	return {.ptr = out, .ec = {}};
}

convert_result<std::byte *>
base64_decoder::convert (const std::byte *first, const std::byte *last, std::byte *out, std::byte *out_last) noexcept
{
	static constexpr auto map = make_decode_map();

	auto n = static_cast<size_t>(last - first);
	if (n == 0)
	{
		return {.ptr = out, .ec = {}};
	}
	if (static_cast<size_t>(out_last - out) < max_size(n))
	{
		return {.ptr = nullptr, .ec = std::errc::value_too_large};
	}
	if (n % 4 != 0)
	{
		return {.ptr = nullptr, .ec = std::errc::illegal_byte_sequence};
	}

	const int pad = static_cast<int>(last[-1] == std::byte{'='}) + static_cast<int>(last[-2] == std::byte{'='});
	const auto *body_end = first + (((n - static_cast<size_t>(pad)) / 4) * 4);

	while (first != body_end)
	{
		auto b0 = map[std::to_integer<uint8_t>(first[0])];
		auto b1 = map[std::to_integer<uint8_t>(first[1])];
		auto b2 = map[std::to_integer<uint8_t>(first[2])];
		auto b3 = map[std::to_integer<uint8_t>(first[3])];

		if (b0 == invalid || b1 == invalid || b2 == invalid || b3 == invalid)
		{
			return {.ptr = nullptr, .ec = std::errc::illegal_byte_sequence};
		}

		auto v = (uint32_t{b0} << 18) | (uint32_t{b1} << 12) | (uint32_t{b2} << 6) | b3;
		*out++ = std::byte{static_cast<uint8_t>(v >> 16)};
		*out++ = std::byte{static_cast<uint8_t>(v >> 8)};
		*out++ = std::byte{static_cast<uint8_t>(v)};
		first += 4;
	}

	if (pad == 1)
	{
		auto b0 = map[std::to_integer<uint8_t>(first[0])];
		auto b1 = map[std::to_integer<uint8_t>(first[1])];
		auto b2 = map[std::to_integer<uint8_t>(first[2])];

		if (b0 == invalid || b1 == invalid || b2 == invalid)
		{
			return {.ptr = nullptr, .ec = std::errc::illegal_byte_sequence};
		}

		auto v = (uint32_t{b0} << 18) | (uint32_t{b1} << 12) | (uint32_t{b2} << 6);
		*out++ = std::byte{static_cast<uint8_t>(v >> 16)};
		*out++ = std::byte{static_cast<uint8_t>(v >> 8)};
	}
	else if (pad == 2)
	{
		auto b0 = map[std::to_integer<uint8_t>(first[0])];
		auto b1 = map[std::to_integer<uint8_t>(first[1])];

		if (b0 == invalid || b1 == invalid)
		{
			return {.ptr = nullptr, .ec = std::errc::illegal_byte_sequence};
		}

		auto v = (uint32_t{b0} << 18) | (uint32_t{b1} << 12);
		*out++ = std::byte{static_cast<uint8_t>(v >> 16)};
	}

	return {.ptr = out, .ec = {}};
}

// NOLINTEND(readability-magic-numbers)

} // namespace pal
