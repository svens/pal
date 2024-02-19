#if !defined(_CRT_SECURE_NO_WARNINGS)
	#define _CRT_SECURE_NO_WARNINGS
#endif
#include <cstdlib>

#include <pal/test>
#include <pal/version>
#include <catch2/catch_session.hpp>

#if __pal_os_windows && __pal_build_debug

int report_hook (int report_type, char *message, int *return_value)
{
	static const char *level[] = { "WARN", "ERROR", "ASSERT" };
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
{ }

#endif

int main (int argc, char *argv[])
{
	set_report_hook();
	return Catch::Session().run(argc, argv);
}

bool pal_test::__on_ci ()
{
	static const bool has_env = (std::getenv("CI") != nullptr);
	return has_env;
}

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
	return std::free(ptr);
}

void operator delete (void *ptr, size_t) noexcept
{
	return std::free(ptr);
}
