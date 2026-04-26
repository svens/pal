#pragma once

/**
 * \file pal/__platform_sdk.hpp
 * Platform SDK includes (internal)
 *
 * Single inclusion point for platform SDK headers that require specific macros
 * to be set before inclusion (e.g. NOMINMAX on Windows).
 */

// Use only preprocessor symbols here to minimise header inclusion order interactions

#if defined(_WIN32) || defined(_WIN64)
	#define WIN32_LEAN_AND_MEAN
	#define NOMINMAX
	#include <winsock2.h>
	#include <mswsock.h>
#endif
