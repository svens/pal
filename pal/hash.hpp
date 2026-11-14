#pragma once

/**
 * \file pal/hash.hpp
 * Non-cryptographic hash functions
 *
 * See ThirdPartySources.txt for copyright notices.
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>

namespace pal
{

// NOLINTBEGIN(readability-magic-numbers)

namespace __hash
{

inline constexpr auto crc32_table = []
{
	std::array<std::array<uint32_t, 256>, 8> table{};
	for (uint32_t i = 0; i < 256; ++i)
	{
		uint32_t c = i;
		for (int bit = 0; bit < 8; ++bit)
		{
			c = (c >> 1) ^ (0xedb88320U & (0U - (c & 1U)));
		}
		table[0][i] = c;
	}
	for (uint32_t i = 0; i < 256; ++i)
	{
		for (size_t slice = 1; slice < table.size(); ++slice)
		{
			uint32_t prev = table[slice - 1][i];
			table[slice][i] = (prev >> 8) ^ table[0][prev & 0xff];
		}
	}
	return table;
}();

} // namespace __hash

/// CRC-32 (IEEE 802.3, reflected polynomial 0xedb88320) over iterator range.
/// Elements must be byte-sized. Pass a previous result as \a crc to continue it
/// over the next range.
template <typename It>
constexpr uint32_t crc32 (It first, It last, uint32_t crc = 0) noexcept
{
	static_assert(sizeof(std::iter_value_t<It>) == 1);
	constexpr auto byte = [](auto v) -> uint32_t { return static_cast<uint8_t>(v); };
	const auto &t = __hash::crc32_table;
	crc = ~crc;
	if constexpr (std::random_access_iterator<It>)
	{
		while (last - first >= 8)
		{
			uint32_t lo = crc
				^ byte(first[0])
				^ (byte(first[1]) << 8)
				^ (byte(first[2]) << 16)
				^ (byte(first[3]) << 24)
			;
			uint32_t hi = byte(first[4])
				^ (byte(first[5]) << 8)
				^ (byte(first[6]) << 16)
				^ (byte(first[7]) << 24)
			;
			crc = t[7][lo & 0xff]
				^ t[6][(lo >> 8) & 0xff]
				^ t[5][(lo >> 16) & 0xff]
				^ t[4][lo >> 24]
				^ t[3][hi & 0xff]
				^ t[2][(hi >> 8) & 0xff]
				^ t[1][(hi >> 16) & 0xff]
				^ t[0][hi >> 24]
			;
			first += 8;
		}
	}
	while (first != last)
	{
		crc = (crc >> 8) ^ t[0][(crc ^ byte(*first++)) & 0xff];
	}
	return ~crc;
}

/// CRC-32 over range with begin/end.
template <typename Range>
constexpr uint32_t crc32 (const Range &range, uint32_t crc = 0) noexcept
{
	return crc32(std::cbegin(range), std::cend(range), crc);
}

/// FNV-1a 64-bit initial basis
constexpr uint64_t fnv_1a_64_init = 0xcbf29ce484222325ULL;

/// FNV-1a 64-bit hash over iterator range.
/// Elements must be byte-sized.
template <typename It>
constexpr uint64_t fnv_1a_64 (It first, It last, uint64_t h = fnv_1a_64_init) noexcept
{
	static_assert(sizeof(std::iter_value_t<It>) == 1);
	while (first != last)
	{
		h ^= static_cast<uint8_t>(*first++);
		h += (h << 1) + (h << 4) + (h << 5) + (h << 7) + (h << 8) + (h << 40);
	}
	return h;
}

/// FNV-1a 64-bit hash over range with begin/end.
template <typename Range>
constexpr uint64_t fnv_1a_64 (const Range &range, uint64_t h = fnv_1a_64_init) noexcept
{
	return fnv_1a_64(std::cbegin(range), std::cend(range), h);
}

/// Reduce two 64-bit values to one.
/// Algorithm from Google CityHash.
constexpr uint64_t hash_128_to_64 (uint64_t h, uint64_t l) noexcept
{
	constexpr uint64_t mul = 0x9ddfea08eb382d69ULL;
	uint64_t a = (l ^ h) * mul;
	a ^= (a >> 47);
	uint64_t b = (h ^ a) * mul;
	b ^= (b >> 47);
	return b * mul;
}

// NOLINTEND(readability-magic-numbers)

} // namespace pal
