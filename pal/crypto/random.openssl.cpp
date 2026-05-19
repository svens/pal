#include <pal/crypto/__crypto.hpp>

#if __pal_crypto_openssl

// clang-format off

#include <pal/crypto/random.hpp>
#include <openssl/rand.h>

namespace pal::crypto::__crypto
{

result<void> random_fill (std::span<std::byte> buf) noexcept
{
	const auto rc = ::RAND_bytes(
		reinterpret_cast<unsigned char *>(buf.data()),
		static_cast<int>(buf.size())
	);

	// LCOV_EXCL_START
	if (rc != 1)
	{
		return make_unexpected(std::errc::io_error);
	}
	// LCOV_EXCL_STOP

	return {};
}

} // namespace pal::crypto::__crypto

// clang-format on

#endif // __pal_crypto_openssl
