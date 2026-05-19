#pragma once

/**
 * \file pal/crypto/random.hpp
 * Cryptographic random generation
 */

#include <pal/buffer.hpp>
#include <pal/result.hpp>
#include <span>

namespace pal::crypto
{

namespace __crypto
{

result<void> random_fill (std::span<std::byte> buf) noexcept;

} // namespace __crypto

/// Fill \a buffer with cryptographically strong random bytes.
template <mutable_buffer Buffer>
	requires(!mutable_buffer_sequence<Buffer>)
result<void> random (Buffer &buffer) noexcept
{
	return __crypto::random_fill(std::as_writable_bytes(std::span{buffer}));
}

/// Fill all \a buffers with cryptographically strong random bytes.
result<void> random (mutable_buffer_sequence auto &buffers) noexcept
{
	for (auto &buffer: buffers)
	{
		if (auto r = __crypto::random_fill(std::as_writable_bytes(std::span{buffer})); !r)
		{
			return r;
		}
	}
	return {};
}

} // namespace pal::crypto
