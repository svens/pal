#pragma once

/**
 * \file pal/__diagnostic.hpp
 * Compiler diagnostic suppression (private)
 */

#include <pal/version.hpp>

#define __pal_pragma(x) _Pragma(#x)

#if __pal_compiler_clang || __pal_compiler_gcc
	#define __pal_diagnostic(x) __pal_pragma(GCC diagnostic x)
	#define __pal_diagnostic_suppress(x) __pal_diagnostic(ignored x)
	#define __pal_diagnostic_warning(gcc, msvc) gcc
#elif __pal_compiler_msvc
	#define __pal_diagnostic(x) __pal_pragma(warning(x))
	#define __pal_diagnostic_suppress(x) __pal_diagnostic(disable: x)
	#define __pal_diagnostic_warning(gcc, msvc) msvc
#endif

#define __pal_aligned_struct_padding __pal_diagnostic_warning("-Wpadded", 4324)
#define __pal_unused_function __pal_diagnostic_warning("-Wunused-function", 4505)
#define __pal_unused_parameter __pal_diagnostic_warning("-Wunused-parameter", 4100)
