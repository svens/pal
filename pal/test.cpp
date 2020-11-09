#define _CRT_SECURE_NO_WARNINGS
#define CATCH_CONFIG_RUNNER

#include <catch2/catch.hpp>
#include <pal/test>
#include <cstdlib>



#if __pal_os_windows && !NDEBUG

int report_hook (int report_type, char *message, int *return_value)
{
	static const char *level[] = { "WARN", "ERROR", "ASSERT" };
	printf("[%s] %s\n", level[report_type], message);
	*return_value = 0;
	return TRUE;
}

void set_report_hook ()
{
	_CrtSetReportHook(report_hook);
}

#else

void set_report_hook ()
{ }

#endif


#if OPENSSL_VERSION_NUMBER >= 0x10100000

void *test_malloc (size_t size, const char *, int)
{
	if (pal_test::bad_alloc_once::fail)
	{
		pal_test::bad_alloc_once::fail = false;
		return nullptr;
	}
	return std::malloc(size);
}

void set_openssl_alloc_hook ()
{
	::CRYPTO_set_mem_functions(
		test_malloc,
		nullptr,
		nullptr
	);
}

#else

void set_openssl_alloc_hook ()
{ }

#endif


int main (int argc, char *argv[])
{
	set_report_hook();
	set_openssl_alloc_hook();
	return Catch::Session().run(argc, argv);
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


void operator delete (void *ptr) noexcept
{
	std::free(ptr);
}


void operator delete (void *ptr, size_t) noexcept
{
	std::free(ptr);
}


namespace pal_test {

bool has_ci_environment_variable ()
{
	static const bool has_env = (std::getenv("CI") != nullptr);
	return has_env;
}

} // namespace pal_test
