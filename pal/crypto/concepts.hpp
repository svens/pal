#pragma once

/**
 * \file pal/crypto/concepts.hpp
 * Cryptographic algorithm concepts
 */

#include <concepts>
#include <cstddef>
#include <string_view>

namespace pal::crypto
{

// clang-format off
template <typename T>
concept digest_algorithm = requires
{
	{ T::digest_size } -> std::convertible_to<size_t>;
	{ T::id } -> std::convertible_to<std::string_view>;
	typename T::hash;
	typename T::hmac;
};
// clang-format on

} // namespace pal::crypto
