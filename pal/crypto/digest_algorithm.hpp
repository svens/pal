#pragma once

/**
 * \file pal/crypto/digest_algorithm.hpp
 * Cryptographic digest algorithm descriptors (internal)
 */

#include <memory>
#include <span>
#include <string_view>
#include <system_error>

namespace pal::crypto::algorithm
{

// clang-format off

// X(tag, name, digest_size)
#define __pal_crypto_digest_algorithm(X) \
	X(md5, MD5, 16) \
	X(sha1, SHA1, 20) \
	X(sha256, SHA256, 32) \
	X(sha384, SHA384, 48) \
	X(sha512, SHA512, 64)

#define __pal_crypto_define_algorithm(Tag, Name, Size) \
	struct Tag \
	{ \
		static constexpr std::string_view id = #Tag; \
		static constexpr size_t digest_size = Size; \
		\
		struct hash \
		{ \
			hash(std::error_code &ec) noexcept; \
			~hash() noexcept; \
			\
			hash(hash &&) noexcept; \
			hash &operator=(hash &&) noexcept; \
			\
			void update(std::span<const std::byte> input) noexcept; \
			void finish(std::span<std::byte, digest_size> digest) noexcept; \
		\
		private: \
			struct impl_type; \
			std::unique_ptr<impl_type> impl; \
		}; \
		\
		struct hmac \
		{ \
			hmac(std::span<const std::byte> key, std::error_code &ec) noexcept; \
			~hmac() noexcept; \
			\
			hmac(hmac &&) noexcept; \
			hmac &operator=(hmac &&) noexcept; \
			\
			void update(std::span<const std::byte> input) noexcept; \
			void finish(std::span<std::byte, digest_size> digest) noexcept; \
		\
		private: \
			struct impl_type; \
			std::unique_ptr<impl_type> impl; \
		}; \
	};

__pal_crypto_digest_algorithm(__pal_crypto_define_algorithm)
#undef __pal_crypto_define_algorithm

// clang-format on

} // namespace pal::crypto::algorithm
