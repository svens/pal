#include <pal/__platform_sdk>
#include <pal/crypto/random>
#include <pal/version>

#if __pal_os_linux
	#include <openssl/rand.h>
#elif __pal_os_macos
	#include <CommonCrypto/CommonCrypto.h>
	#include <CommonCrypto/CommonRandom.h>
#elif __pal_os_windows
	#include <bcrypt.h>
#endif

namespace pal::crypto::__random {

void fill (void *data, size_t size) noexcept
{
	#if __pal_os_linux
	{
		::RAND_bytes(static_cast<unsigned char *>(data), size);
	}
	#elif __pal_os_macos
	{
		::CCRandomGenerateBytes(data, size);
	}
	#elif __pal_os_windows
	{
		(void)::BCryptGenRandom(nullptr,
			static_cast<PUCHAR>(data),
			static_cast<ULONG>(size),
			BCRYPT_USE_SYSTEM_PREFERRED_RNG
		);
	}
	#endif
}

} // namespace pal::crypto::__random
