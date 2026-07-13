#ifndef _CRT_SECURE_NO_WARNINGS
	#define _CRT_SECURE_NO_WARNINGS
#endif
#include <cstdlib>

#include <pal/test.hpp>
#include <pal/version.hpp>
#include <pal/__diagnostic.hpp>
#include <catch2/catch_session.hpp>
#include <csetjmp>
#include <cstdio>
#include <exception>

#if __pal_os_windows
	#include <io.h>
	#define __pal_test_dup _dup
	#define __pal_test_dup2 _dup2
	#define __pal_test_fileno _fileno
	#define __pal_test_close _close
#else
	#include <unistd.h>
	#define __pal_test_dup dup
	#define __pal_test_dup2 dup2
	#define __pal_test_fileno fileno
	#define __pal_test_close close
#endif

// LeakSanitizer availability: gcc defines __SANITIZE_ADDRESS__, clang answers __has_feature; MSVC ASan
// has no leak detection, so Windows stays out even though it defines __SANITIZE_ADDRESS__ too.
#if !__pal_os_windows
	#ifdef __SANITIZE_ADDRESS__
		#define __pal_test_lsan 1
	#elifdef __has_feature
		#if __has_feature(address_sanitizer)
			#define __pal_test_lsan 1
		#endif
	#endif
#endif
#ifndef __pal_test_lsan
	#define __pal_test_lsan 0
#endif
#if __pal_test_lsan
	#include <sanitizer/lsan_interface.h>
#endif

namespace
{

#if __pal_os_windows && __pal_build_debug

int report_hook (int report_type, char *message, int *return_value)
{
	static const char *level[] = {"WARN", "ERROR", "ASSERT"};
	printf("[%s] %s\n", level[report_type], message);
	*return_value = 0;
	return true;
}

void set_report_hook ()
{
	_CrtSetReportHook(report_hook);
}

#else

void set_report_hook ()
{
}

#endif

} // namespace

int main (int argc, char *argv[])
{
	set_report_hook();
	return Catch::Session().run(argc, argv);
}

bool pal_test::on_ci_impl () noexcept
{
	static const bool has_env = (std::getenv("CI") != nullptr);
	return has_env;
}

// NOLINTBEGIN(modernize-avoid-setjmp-longjmp)

namespace
{

thread_local std::jmp_buf *terminate_env = nullptr;
thread_local bool terminate_trapped = false;

[[noreturn]] void on_terminate () noexcept
{
	if (terminate_env != nullptr)
	{
		terminate_trapped = true;
		std::longjmp(*terminate_env, 1);
	}
	std::abort();
}

} // namespace

std::pair<bool, std::string> pal_test::__require_terminate (void (*fn)(void *context), void *context)
{
	auto *previous_env = terminate_env;
	auto previous_trapped = terminate_trapped;
	auto previous_handler = std::set_terminate(&on_terminate);

	std::jmp_buf env;
	terminate_env = &env;
	terminate_trapped = false;

	// redirect stderr to a temp file for the duration of the call, so a
	// pal_require() failure's diagnostic output doesn't interleave with
	// Catch2's own reporting; captured back into a string below instead.
	std::fflush(stderr);
	auto *capture_file = std::tmpfile();
	auto saved_stderr_fd = __pal_test_dup(__pal_test_fileno(stderr));
	__pal_test_dup2(__pal_test_fileno(capture_file), __pal_test_fileno(stderr));

	bool trapped;

	// the trap path leaks fn's locals by design (longjmp skips destructors -- see require_terminate());
	// hide allocations made under fn from LeakSanitizer, keeping the rest of the binary leak-checked
	#if __pal_test_lsan
	{
		__lsan_disable();
	}
	#endif

	__pal_diagnostic(push)
	{
		__pal_diagnostic_suppress(__pal_cpp_dtor_setjmp);

		if (setjmp(env) == 0)
		{
			#if __pal_os_windows
			{
				// MSVC's longjmp unwinds the stack like an exception, running destructors between the
				// longjmp and this setjmp -- defeating the deliberate leak-on-trap containment (see
				// require_terminate()). A zeroed saved frame pointer makes longjmp skip unwinding,
				// restoring POSIX semantics.
				reinterpret_cast<_JUMP_BUFFER *>(&env)->Frame = 0;
			}
			#endif
			fn(context);
			trapped = false;
		}
		else
		{
			trapped = terminate_trapped;
		}
	}
	__pal_diagnostic(pop);

	#if __pal_test_lsan
	{
		__lsan_enable();
	}
	#endif

	std::fflush(stderr);
	__pal_test_dup2(saved_stderr_fd, __pal_test_fileno(stderr));
	__pal_test_close(saved_stderr_fd);

	std::string stderr_text;
	auto size = std::ftell(capture_file);
	if (size > 0)
	{
		stderr_text.resize(static_cast<size_t>(size));
		std::ignore = std::fseek(capture_file, 0, SEEK_SET);
		std::ignore = std::fread(stderr_text.data(), 1, stderr_text.size(), capture_file);
	}
	std::fclose(capture_file);

	std::set_terminate(previous_handler);
	terminate_env = previous_env;
	terminate_trapped = previous_trapped;

	return {trapped, std::move(stderr_text)};
}

// NOLINTEND(modernize-avoid-setjmp-longjmp)

// NOLINTBEGIN(readability-inconsistent-declaration-parameter-name)

void *operator new (size_t size)
{
	if (pal_test::bad_alloc_once::fail)
	{
		pal_test::bad_alloc_once::fail = false;
		throw std::bad_alloc();
	}
	return std::malloc(size);
}

void *operator new (size_t size, const std::nothrow_t &) noexcept
{
	if (pal_test::bad_alloc_once::fail)
	{
		pal_test::bad_alloc_once::fail = false;
		return nullptr;
	}
	return std::malloc(size);
}

void operator delete (void *ptr) noexcept
{
	std::free(ptr);
}

void operator delete (void *ptr, size_t) noexcept
{
	std::free(ptr);
}

// NOLINTEND(readability-inconsistent-declaration-parameter-name)
