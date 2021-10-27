#include <pal/result>
#include <pal/test>
#include <catch2/catch_test_macros.hpp>


namespace {


pal::result<int> get_result (int x)
{
	if (x == 1)
	{
		return pal::unexpected{std::make_error_code(std::errc::not_enough_memory)};
	}
	return x;
}


pal::expected<int, bool> get_expected (int x)
{
	if (x == 1)
	{
		return pal::unexpected{false};
	}
	return x;
}


TEST_CASE("pal_try")
{
	SECTION("not expected<T,E>")
	{
		CHECK(pal_try(1) == 1);
	}

	SECTION("value")
	{
		CHECK(pal_try(get_result(2)) == 2);
		CHECK_NOTHROW(pal_try(get_result(2).map([](int){})));
		CHECK(pal_try(get_expected(2)) == 2);
	}

	SECTION("throw")
	{
		CHECK_THROWS_AS(pal_try(get_result(1)), std::system_error);
		CHECK_THROWS_AS(pal_try(get_result(1).map([](int){})), std::system_error);
		CHECK_THROWS_AS(pal_try(get_expected(1)), bool);
	}
}


} // namespace
