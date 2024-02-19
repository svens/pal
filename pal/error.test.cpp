#include <pal/error>
#include <pal/test>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <type_traits>

#include <pal/version>
#if __pal_os_linux || __pal_os_macos
	#include <cerrno>
	auto &expected_category = std::generic_category();
#elif __pal_os_windows
	#include <windows.h>
	auto &expected_category = std::system_category();
#endif

namespace {

TEST_CASE("error")
{
	#define __pal_errc_value(Code, Message) pal::errc::Code,

	SECTION("errc")
	{
		std::error_code ec = GENERATE(values({__pal_errc(__pal_errc_value)}));
		CAPTURE(ec);

		SECTION("message")
		{
			CHECK_FALSE(ec.message().empty());
			CHECK(ec.message() != "unknown");
			CHECK(ec.category() == pal::error_category());
			CHECK(ec.category().name() == std::string{"pal"});
		}
	}

	SECTION("unknown")
	{
		std::error_code ec = static_cast<pal::errc>(
			(std::numeric_limits<std::underlying_type_t<pal::errc>>::max)()
		);
		CHECK(ec.message() == "unknown");
		CHECK(ec.category() == pal::error_category());
		CHECK(ec.category().name() == std::string{"pal"});
	}

	SECTION("last_system_error")
	{
		#if __pal_os_linux || __pal_os_macos
			errno = ENOMEM;
		#elif __pal_os_windows
			SetLastError(ENOMEM);
		#endif

		auto error = pal::this_thread::last_system_error();
		CHECK(error.value() == ENOMEM);
		CHECK(error.category() == expected_category);
	}
}

} // namespace
