#include <pal/version>

#if __pal_os_linux

#include <pal/crypto/__algorithm>

#define OPENSSL_SUPPRESS_DEPRECATED
#include <openssl/md5.h>

namespace pal::crypto::__algorithm {

#define __pal_crypto_algorithm_impl(Algorithm, Prefix) \
	struct Algorithm ## _hash::impl_type: ::Prefix ## _CTX {}; \
	\
	Algorithm ## _hash::~Algorithm ## _hash () noexcept = default; \
	\
	Algorithm ## _hash::Algorithm ## _hash () \
		: impl{std::make_unique<impl_type>()} \
	{ \
		::Prefix ## _Init(impl.get()); \
	} \
	\
	void Algorithm ## _hash::update (const void *ptr, size_t size) noexcept \
	{ \
		::Prefix ## _Update(impl.get(), ptr, size); \
	} \
	\
	void Algorithm ## _hash::finish (void *digest) noexcept \
	{ \
		::Prefix ## _Final(static_cast<uint8_t *>(digest), impl.get()); \
		::Prefix ## _Init(impl.get()); \
	}

__pal_crypto_algorithm(__pal_crypto_algorithm_impl);

#undef __pal_crypto_algorithm_impl

} // namespace pal::crypto::__algorithm

#endif // __pal_os_linux
