#include <pal/error>
#include <pal/test>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <type_traits>

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
			std::numeric_limits<std::underlying_type_t<pal::errc>>::max()
		);
		CHECK(ec.message() == "unknown");
		CHECK(ec.category() == pal::error_category());
		CHECK(ec.category().name() == std::string{"pal"});
	}
}

} // namespace
