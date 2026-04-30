#pragma once

/**
 * \file pal/hash.hpp
 * Non-cryptographic hash functions
 *
 * See ThirdPartySources.txt for copyright notices.
 */

#include <cstdint>
#include <iterator>

namespace pal
{

// NOLINTBEGIN(readability-magic-numbers)

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
