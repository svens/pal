#pragma once

/**
 * \file pal/byte_order.hpp
 * Host/network byte order conversions
 */

#include <bit>
#include <concepts>

namespace pal
{

/// Convert \a value from host to network order.
template <std::integral T>
constexpr T hton (T value) noexcept
{
	if constexpr (std::endian::native == std::endian::little)
	{
		return std::byteswap(value);
	}
	else
	{
		return value;
	}
}

/// Convert \a value from network to host order.
template <std::integral T>
constexpr T ntoh (T value) noexcept
{
	return hton(value);
}

} // namespace pal
