#include <pal/crypto/__crypto.hpp>

#if __pal_crypto_openssl

// clang-format off

#include <pal/crypto/digest_algorithm.hpp>
#define OPENSSL_SUPPRESS_DEPRECATED
#include <openssl/md5.h>
#include <openssl/sha.h>
#include <openssl/hmac.h>
#undef OPENSSL_SUPPRESS_DEPRECATED
#include <memory>

namespace
{

using SHA1_CTX = ::SHA_CTX;
using SHA384_CTX = ::SHA512_CTX;

} // namespace

namespace pal::crypto::algorithm
{

//
// Hash
//

#define __pal_crypto_impl_hash(Tag, Name, Size) \
	struct Tag::hash::impl_type: ::Name##_CTX {}; \
	\
	Tag::hash::~hash () noexcept = default; \
	Tag::hash::hash (hash &&) noexcept = default; \
	Tag::hash &Tag::hash::operator= (hash &&) noexcept = default; \
	\
	Tag::hash::hash (std::error_code &ec) noexcept \
		: impl{new(std::nothrow) impl_type} \
	{ \
		if (impl) \
		{ \
			::Name##_Init(impl.get()); \
			ec.clear(); \
		} \
		else \
		{ \
			ec = std::make_error_code(std::errc::not_enough_memory); \
		} \
	} \
	\
	void Tag::hash::update (std::span<const std::byte> input) noexcept \
	{ \
		::Name##_Update(impl.get(), input.data(), input.size()); \
	} \
	\
	void Tag::hash::finish (std::span<std::byte, Size> digest) noexcept \
	{ \
		::Name##_Final(reinterpret_cast<unsigned char *>(digest.data()), impl.get()); \
		::Name##_Init(impl.get()); \
	}

__pal_crypto_digest_algorithm(__pal_crypto_impl_hash)
#undef __pal_crypto_impl_hash

//
// HMAC
//

#define __pal_crypto_impl_hmac(Tag, Name, Size) \
	struct Tag::hmac::impl_type \
	{ \
		std::unique_ptr<::HMAC_CTX, decltype(&::HMAC_CTX_free)> ctx{::HMAC_CTX_new(), &::HMAC_CTX_free}; \
		\
		impl_type (const void *key, size_t key_size) noexcept \
		{ \
			if (ctx) \
			{ \
				if (key == nullptr) \
				{ \
					key = ""; \
					key_size = 0; \
				} \
				::HMAC_Init_ex(ctx.get(), key, key_size, ::EVP_##Tag(), nullptr); \
			} \
		} \
	}; \
	\
	Tag::hmac::~hmac () noexcept = default; \
	Tag::hmac::hmac (hmac &&) noexcept = default; \
	Tag::hmac &Tag::hmac::operator= (hmac &&) noexcept = default; \
	\
	Tag::hmac::hmac (std::span<const std::byte> key, std::error_code &ec) noexcept \
		: impl{new(std::nothrow) impl_type(key.data(), key.size())} \
	{ \
		if (impl && impl->ctx) \
		{ \
			ec.clear(); \
		} \
		else \
		{ \
			ec = std::make_error_code(std::errc::not_enough_memory); \
		} \
	} \
	\
	void Tag::hmac::update (std::span<const std::byte> input) noexcept \
	{ \
		::HMAC_Update(impl->ctx.get(), reinterpret_cast<const uint8_t *>(input.data()), input.size()); \
	} \
	\
	void Tag::hmac::finish (std::span<std::byte, Size> digest) noexcept \
	{ \
		::HMAC_Final(impl->ctx.get(), reinterpret_cast<uint8_t *>(digest.data()), nullptr); \
		::HMAC_Init_ex(impl->ctx.get(), nullptr, 0, nullptr, nullptr); \
	}

__pal_crypto_digest_algorithm(__pal_crypto_impl_hmac)
#undef __pal_crypto_impl_hmac

} // namespace pal::crypto::algorithm

// clang-format on

#endif // __pal_crypto_openssl
