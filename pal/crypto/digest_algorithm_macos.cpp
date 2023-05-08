#include <pal/version>

#if __pal_os_macos

#include <pal/crypto/digest_algorithm>
#include <CommonCrypto/CommonDigest.h>
#include <CommonCrypto/CommonHMAC.h>

namespace pal::crypto::algorithm {

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
#pragma GCC diagnostic pop

} // namespace pal::crypto::algorithm

#endif // __pal_os_macos
