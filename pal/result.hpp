#pragma once

/**
 * \file pal/result.hpp
 * Convenience types pal::result<T> and pal::unexpected
 */

#include <expected>
#include <system_error>

namespace pal
{

template <typename T>
using result = std::expected<T, std::error_code>;

using unexpected = std::unexpected<std::error_code>;

inline unexpected make_unexpected (std::errc ec) noexcept
{
	return unexpected{std::make_error_code(ec)};
}

} // namespace pal
