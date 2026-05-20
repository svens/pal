#pragma once

/**
 * \file pal/crypto/random.hpp
 * Cryptographic random generation
 */

#include <pal/buffer.hpp>
#include <pal/result.hpp>
#include <span>
#include <type_traits>

namespace pal::crypto
{

namespace __crypto
{

result<void> random_fill (std::span<std::byte> buf) noexcept;

} // namespace __crypto

/// Fill \a buffer with cryptographically strong random bytes.
result<void> random (mutable_buffer auto &buffer) noexcept
{
	return __crypto::random_fill(std::as_writable_bytes(std::span{buffer}));
}

/// Return cryptographically strong random value of trivially copyable type \a T.
template <typename T>
	requires std::is_trivially_copyable_v<T>
result<T> random () noexcept
{
	T value;
	return __crypto::random_fill(std::as_writable_bytes(std::span{&value, 1})).transform([&] { return value; });
}

} // namespace pal::crypto
