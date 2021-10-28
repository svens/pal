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


	SECTION("make_unexpected")
	{
		auto errc = GENERATE(values({__pal_errc(__pal_errc_value)}));
		auto u = pal::make_unexpected(errc);
		CHECK(u.value().message() != "unknown");
		CHECK(u.value().category() == pal::error_category());
		CHECK(u.value().category().name() == std::string{"pal"});
	}


	SECTION("throw_on_error - do not throw")
	{
		auto do_not_throw = []()
		{
			pal::throw_on_error x("message");
		};
		CHECK_NOTHROW(do_not_throw());
	}


	SECTION("throw_on_error - do throw")
	{
		auto do_throw = []()
		{
			pal::throw_on_error x("message");
			static_cast<std::error_code &>(x) =
				std::make_error_code(std::errc::not_enough_memory);
		};
		CHECK_THROWS_AS(do_throw(), std::system_error);
	}
}


} // namespace
