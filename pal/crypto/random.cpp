#include <pal/__bits/platform_sdk>
#include <pal/crypto/random>

#if __pal_os_linux
	#include <openssl/rand.h>
#elif __pal_os_macos
	#include <CommonCrypto/CommonCrypto.h>
	#include <CommonCrypto/CommonRandom.h>
#elif __pal_os_windows
	#include <bcrypt.h>
	#pragma comment(lib, "bcrypt")
#endif


__pal_begin


namespace crypto::__bits {


void random (void *data, size_t size) noexcept
{
	#if __pal_os_linux
		::RAND_bytes(static_cast<unsigned char *>(data), size);
	#elif __pal_os_macos
		::CCRandomGenerateBytes(data, size);
	#elif __pal_os_windows
		::BCryptGenRandom(nullptr,
			static_cast<PUCHAR>(data),
			static_cast<ULONG>(size),
			BCRYPT_USE_SYSTEM_PREFERRED_RNG
		);
	#endif
}


} // namespace crypto::__bits


__pal_end
