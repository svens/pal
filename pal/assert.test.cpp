#include <pal/assert>
#include <pal/test>


namespace {


TEST_CASE("assert")
{
	SECTION("assert_true")
	{
		REQUIRE_NOTHROW(pal_assert(true));
	}

	SECTION("assert_false")
	{
		if constexpr (pal::assert_noexcept)
		{
			REQUIRE_NOTHROW(pal_assert(false));
		}
		else
		{
			REQUIRE_THROWS_AS(pal_assert(false), std::logic_error);
		}
	}
}


} // namespace
