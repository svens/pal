#include <pal/version>

#if __pal_os_macos

#include <pal/crypto/__algorithm>
#include <CommonCrypto/CommonDigest.h>
#include <CommonCrypto/CommonHMAC.h>

namespace pal::crypto::__algorithm {

#define __pal_crypto_algorithm_impl(Algorithm, Prefix) \
	struct Algorithm ## _hash::impl_type: ::CC_ ## Prefix ## _CTX {}; \
	\
	Algorithm ## _hash::~Algorithm ## _hash () noexcept = default; \
	\
	Algorithm ## _hash::Algorithm ## _hash () \
		: impl{std::make_unique<impl_type>()} \
	{ \
		::CC_ ## Prefix ## _Init(impl.get()); \
	} \
	\
	void Algorithm ## _hash::update (const void *ptr, size_t size) noexcept \
	{ \
		::CC_ ## Prefix ## _Update(impl.get(), ptr, size); \
	} \
	\
	void Algorithm ## _hash::finish (void *digest) noexcept \
	{ \
		::CC_ ## Prefix ## _Final(static_cast<uint8_t *>(digest), impl.get()); \
		::CC_ ## Prefix ## _Init(impl.get()); \
	}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
__pal_crypto_algorithm(__pal_crypto_algorithm_impl);
#pragma GCC diagnostic pop

#undef __pal_crypto_algorithm_impl

} // namespace pal::crypto::__algorithm

#endif // __pal_os_macos
