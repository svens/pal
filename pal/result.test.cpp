#include <pal/result>
#include <pal/test>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

namespace {

pal::result<bool> value (bool ok)
{
	if (!ok)
	{
		return pal::make_unexpected(std::errc::not_enough_memory);
	}
	return true;
}

TEST_CASE("value_or_throw")
{
	SECTION("value")
	{
		CHECK(pal::value_or_throw(value(true)));
		CHECK_NOTHROW(pal::value_or_throw(value(true).transform([](bool){})));
	}

	SECTION("throw")
	{
		CHECK_THROWS_AS(pal::value_or_throw(value(false)), std::system_error);
		CHECK_THROWS_AS(pal::value_or_throw(value(false).transform([](bool){})), std::system_error);
	}
}

} // namespace
