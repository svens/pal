// -*- C++ -*-

#include <CommonCrypto/CommonDigest.h>
#include <CommonCrypto/CommonHMAC.h>

namespace {

// help machinery below with CommonCrypto naming inconsistency
using CC_SHA384_CTX = ::CC_SHA512_CTX;

} // namespace

namespace pal::crypto::algorithm {

//
// Hash
//

#define __pal_crypto_digest_algorithm_impl(Algorithm, Context, Size) \
	struct Algorithm::hash::impl_type: ::CC_ ## Context ## _CTX { }; \
	\
	Algorithm::hash::~hash () noexcept = default; \
	Algorithm::hash::hash (hash &&) noexcept = default; \
	Algorithm::hash &Algorithm::hash::operator= (hash &&) noexcept = default; \
	\
	Algorithm::hash::hash (std::error_code &error) noexcept \
		: impl{new(std::nothrow) impl_type} \
	{ \
		if (!impl) \
		{ \
			error = std::make_error_code(std::errc::not_enough_memory); \
		} \
		else \
		{ \
			::CC_ ## Context ## _Init(impl.get()); \
			error.clear(); \
		} \
	} \
	\
	void Algorithm::hash::update (std::span<const std::byte> input) noexcept \
	{ \
		::CC_ ## Context ## _Update(impl.get(), input.data(), input.size()); \
	} \
	\
	void Algorithm::hash::finish (std::span<std::byte, digest_size> digest) noexcept \
	{ \
		::CC_ ## Context ## _Final(reinterpret_cast<uint8_t *>(digest.data()), impl.get()); \
		::CC_ ## Context ## _Init(impl.get()); \
	}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
__pal_crypto_digest_algorithm(__pal_crypto_digest_algorithm_impl)
#undef __pal_crypto_digest_algorithm_impl
#pragma GCC diagnostic pop

//
// HMAC
//

#define __pal_crypto_digest_algorithm_impl(Algorithm, Context, Size) \
	struct Algorithm::hmac::impl_type \
	{ \
		CCHmacContext init_ctx{}, ctx{}; \
		\
		impl_type (std::span<const std::byte> key) noexcept \
		{ \
			::CCHmacInit(&init_ctx, ::kCCHmacAlg ## Context, key.data(), key.size()); \
			ctx = init_ctx; \
		} \
	}; \
	\
	Algorithm::hmac::~hmac () noexcept = default; \
	Algorithm::hmac::hmac (hmac &&) noexcept = default; \
	Algorithm::hmac &Algorithm::hmac::operator= (hmac &&) noexcept = default; \
	\
	Algorithm::hmac::hmac (std::span<const std::byte> key, std::error_code &error) noexcept \
		: impl{new(std::nothrow) impl_type(key)} \
	{ \
		if (!impl) \
		{ \
			error = std::make_error_code(std::errc::not_enough_memory); \
		} \
	} \
	\
	void Algorithm::hmac::update (std::span<const std::byte> input) noexcept \
	{ \
		::CCHmacUpdate(&impl->ctx, input.data(), input.size()); \
	} \
	\
	void Algorithm::hmac::finish (std::span<std::byte, digest_size> digest) noexcept \
	{ \
		::CCHmacFinal(&impl->ctx, digest.data()); \
		impl->ctx = impl->init_ctx; \
	}

__pal_crypto_digest_algorithm(__pal_crypto_digest_algorithm_impl)
#undef __pal_crypto_digest_algorithm_impl

} // namespace pal::crypto::algorithm
