#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <version>

namespace pal
{

//
// project version
//

extern const std::string_view version;
extern const int version_major, version_minor, version_patch;

//
// build type
//

#define __pal_build_debug 0
#define __pal_build_release 0

#if defined(NDEBUG) && !defined(_DEBUG)
	#undef __pal_build_release
	#define __pal_build_release 1
#else
	#undef __pal_build_debug
	#define __pal_build_debug 1
#endif

enum class build_type
{
	debug,
	release,
};

constexpr build_type build =
#if __pal_build_debug
	build_type::debug
#elif __pal_build_release
	build_type::release
#endif
	;

//
// compiler
//

#define __pal_compiler_clang 0
#define __pal_compiler_gcc 0
#define __pal_compiler_msvc 0

#ifdef __clang__
	#undef __pal_compiler_clang
	#define __pal_compiler_clang 1
#elifdef __GNUC__
	#undef __pal_compiler_gcc
	#define __pal_compiler_gcc 1
#elifdef _MSC_VER
	#undef __pal_compiler_msvc
	#define __pal_compiler_msvc 1
#else
	#error "Unsupported compiler"
#endif

enum class compiler_type
{
	clang,
	gcc,
	msvc,
};

constexpr compiler_type compiler =
#if __pal_compiler_clang
	compiler_type::clang
#elif __pal_compiler_gcc
	compiler_type::gcc
#elif __pal_compiler_msvc
	compiler_type::msvc
#endif
	;

//
// build OS
//

#define __pal_os_linux 0
#define __pal_os_macos 0
#define __pal_os_windows 0

#ifdef __linux__
	#undef __pal_os_linux
	#define __pal_os_linux 1
#elifdef __APPLE__
	#undef __pal_os_macos
	#define __pal_os_macos 1
#elif defined(_WIN32) || defined(_WIN64)
	#undef __pal_os_windows
	#define __pal_os_windows 1
#else
	#error "Unsupported OS"
#endif

enum class os_type
{
	linux,
	macos,
	windows,
};

constexpr os_type os =
#if __pal_os_linux
	os_type::linux
#elif __pal_os_macos
	os_type::macos
#elif __pal_os_windows
	os_type::windows
#endif
	;

//
// hardware
//

#define __pal_arch_x86 0
#define __pal_arch_x86_64 0
#define __pal_arch_arm64 0

#if defined(__x86_64__) || defined(_M_X64)
	#undef __pal_arch_x86_64
	#define __pal_arch_x86_64 1
#elif defined(__aarch64__) || defined(_M_ARM64)
	#undef __pal_arch_arm64
	#define __pal_arch_arm64 1
#elif defined(__i386__) || defined(_M_IX86)
	#undef __pal_arch_x86
	#define __pal_arch_x86 1
#else
	#error "Unsupported hardware"
#endif

enum class arch_type
{
	x86,
	x86_64,
	arm64,
};

constexpr arch_type arch =
#if __pal_arch_x86
	arch_type::x86
#elif __pal_arch_x86_64
	arch_type::x86_64
#elif __pal_arch_arm64
	arch_type::arm64
#endif
	;

// std::hardware_destructive_interference_size reflects the build host, not
// the target, so it cannot be used in portable code
constexpr size_t cache_line_size = 64;

} // namespace pal
