#include <pal/error>
#include <pal/test>
#include <type_traits>


namespace {


TEST_CASE("error")
{
	SECTION("errc")
	{
		#define __pal_errc_value(code, message) pal::errc::code,
		std::error_code ec = GENERATE(values({__pal_errc(__pal_errc_value)}));
		#undef __pal_errc_value
		CAPTURE(ec);

		SECTION("message")
		{
			CHECK_FALSE(ec.message().empty());
			CHECK(ec.message() != "unknown");
			CHECK(ec.category() == pal::error_category());
			CHECK(ec.category().name() == std::string{"pal"});
		}
	}


	SECTION("message_bad_alloc")
	{
		std::error_code ec = pal::errc::__0;
		pal_test::bad_alloc_once x;
		CHECK_THROWS_AS(ec.message(), std::bad_alloc);
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
