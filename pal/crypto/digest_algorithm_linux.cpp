#include <pal/version>

#if __pal_os_linux

#include <pal/crypto/digest_algorithm>

#define OPENSSL_SUPPRESS_DEPRECATED
#include <openssl/md5.h>
#undef OPENSSL_SUPPRESS_DEPRECATED

#include <openssl/sha.h>

namespace {

// help machinery below with OpenSSL naming inconsistency
using SHA1_CTX = ::SHA_CTX;
using SHA384_CTX = ::SHA512_CTX;

} // namespace

namespace pal::crypto::algorithm {

#define __pal_crypto_digest_algorithm_impl(Algorithm, Context, Size) \
	struct Algorithm::hash::impl_type: ::Context ## _CTX { }; \
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
			::Context ## _Init(impl.get()); \
			error.clear(); \
		} \
	} \
	\
	void Algorithm::hash::update (std::span<const std::byte> input) noexcept \
	{ \
		::Context ## _Update(impl.get(), input.data(), input.size()); \
	} \
	\
	void Algorithm::hash::finish (std::span<std::byte, digest_size> digest) noexcept \
	{ \
		::Context ## _Final(reinterpret_cast<uint8_t *>(digest.data()), impl.get()); \
		::Context ## _Init(impl.get()); \
	}

__pal_crypto_digest_algorithm(__pal_crypto_digest_algorithm_impl)

} // namespace pal::crypto::algorithm

#endif // __pal_os_linux
