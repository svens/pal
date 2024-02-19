#include <pal/__platform_sdk>
#include <pal/crypto/digest_algorithm>
#include <pal/version>

#if __pal_os_linux
	#include <pal/crypto/digest_algorithm_linux.ipp>
#elif __pal_os_macos
	#include <pal/crypto/digest_algorithm_macos.ipp>
#elif __pal_os_windows
	#include <pal/crypto/digest_algorithm_windows.ipp>
#endif
