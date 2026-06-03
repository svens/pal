#pragma once

/**
 * \file pal/crypto/__crypto.hpp
 * Platform crypto abstraction (internal)
 */

#include <pal/version.hpp>

#define __pal_crypto_openssl 0
#define __pal_crypto_windows 0

#if __pal_os_linux || __pal_os_macos
	#undef __pal_crypto_openssl
	#define __pal_crypto_openssl 1
#elif __pal_os_windows
	#undef __pal_crypto_windows
	#define __pal_crypto_windows 1
#endif

#if __pal_crypto_windows
	#ifndef WIN32_LEAN_AND_MEAN
		#define WIN32_LEAN_AND_MEAN
	#endif
	#define WIN32_NO_STATUS
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
	#include <windows.h>
	#undef WIN32_NO_STATUS
	#include <winternl.h>
	#include <ntstatus.h>
#endif
