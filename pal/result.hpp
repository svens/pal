#pragma once

/**
 * \file pal/result.hpp
 * Convenience type pal::result<T> for std::expected<T, std::error_code>
 */

#include <expected>
#include <system_error>

namespace pal
{

template <typename T>
using result = std::expected<T, std::error_code>;

inline std::unexpected<std::error_code> make_unexpected (std::errc ec) noexcept
{
	return std::unexpected{std::make_error_code(ec)};
}

} // namespace pal
