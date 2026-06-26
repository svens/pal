#include <pal/crypto/__crypto.hpp>

#if __pal_crypto_openssl

#include <pal/crypto/random.hpp>
#include <openssl/rand.h>

namespace pal::crypto::__crypto
{

result<void> random_fill (std::span<std::byte> buf) noexcept
{
	const auto result = ::RAND_bytes(reinterpret_cast<unsigned char *>(buf.data()), static_cast<int>(buf.size()));

	// LCOV_EXCL_START
	if (result != 1)
	{
		return make_unexpected(std::errc::io_error);
	}
	// LCOV_EXCL_STOP

	return {};
}

} // namespace pal::crypto::__crypto

#endif // __pal_crypto_openssl
