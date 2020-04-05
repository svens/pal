#include <pal/assert>
#include <pal/test>


namespace {


bool fn_with_side_effect (bool &side_effect, bool return_value)
{
	side_effect = true;
	return return_value;
}


TEST_CASE("assert")
{
	bool side_effect = false;

	SECTION("assert_true")
	{
		REQUIRE_NOTHROW(
			pal_assert(fn_with_side_effect(side_effect, true))
		);
	}

	SECTION("assert_false")
	{
		if constexpr (pal::assert_noexcept)
		{
			REQUIRE_NOTHROW(
				pal_assert(fn_with_side_effect(side_effect, false))
			);
		}
		else
		{
			REQUIRE_THROWS_AS(
				pal_assert(fn_with_side_effect(side_effect, false)),
				std::logic_error
			);
		}
	}

	CHECK(side_effect == true);
}


} // namespace
