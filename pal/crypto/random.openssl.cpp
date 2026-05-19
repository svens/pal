#include <pal/crypto/__crypto.hpp>

#if __pal_crypto_openssl

// clang-format off
#include <pal/crypto/random.hpp>
#include <openssl/rand.h>
// clang-format on

namespace pal::crypto::__crypto
{

result<void> random_fill (std::span<std::byte> buf) noexcept
{
	if (::RAND_bytes(reinterpret_cast<unsigned char *>(buf.data()), static_cast<int>(buf.size())) == 1)
	{
		return {};
	}
	return make_unexpected(std::errc::io_error);
}

} // namespace pal::crypto::__crypto

#endif // __pal_crypto_openssl
