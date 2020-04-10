#include <pal/expect>
#include <pal/test>


#include <iostream>


namespace {


bool bool_fn (bool &side_effect, bool return_value)
{
	side_effect = true;
	return return_value;
}


TEST_CASE("expect")
{
	SECTION("bool")
	{
		bool side_effect = false;

		SECTION("true")
		{
			REQUIRE_NOTHROW(pal_expect(bool_fn(side_effect, true)));
			CHECK(pal_expect(bool_fn(side_effect, true)) == true);
		}

		SECTION("false")
		{
			if constexpr (pal::expect_noexcept)
			{
				REQUIRE_NOTHROW(pal_expect(bool_fn(side_effect, false)));
				CHECK(pal_expect(bool_fn(side_effect, false)) == false);
			}
			else
			{
				REQUIRE_THROWS_AS(
					pal_expect(bool_fn(side_effect, false)),
					std::logic_error
				);
			}
		}

		CHECK(side_effect == true);
	}

	SECTION("pointer")
	{
		SECTION("not_nullptr")
		{
			const char buf[] = "a", *p = buf;
			REQUIRE_NOTHROW(pal_expect(p));
			CHECK(pal_expect(p) == buf);
		}

		SECTION("nullptr")
		{
			const char *p = nullptr;
			if constexpr (pal::expect_noexcept)
			{
				REQUIRE_NOTHROW(pal_expect(p));
				CHECK(pal_expect(p) == nullptr);
			}
			else
			{
				REQUIRE_THROWS_AS(pal_expect(p), std::logic_error);
			}
		}
	}

	SECTION("unique_ptr")
	{
		SECTION("not_nullptr")
		{
			auto buf = new char[3];
			std::unique_ptr<char> p{buf};
			REQUIRE_NOTHROW(pal_expect(p));
			CHECK(pal_expect(p) == buf);
		}

		SECTION("nullptr")
		{
			std::unique_ptr<char> p;
			if constexpr (pal::expect_noexcept)
			{
				REQUIRE_NOTHROW(pal_expect(p));
				CHECK(pal_expect(p) == nullptr);
			}
			else
			{
				REQUIRE_THROWS_AS(pal_expect(p), std::logic_error);
			}
		}
	}

	SECTION("shared_ptr")
	{
		SECTION("not_nullptr")
		{
			auto buf = new char[3];
			std::shared_ptr<char> p{buf};
			REQUIRE_NOTHROW(pal_expect(p));
			CHECK(pal_expect(p) == buf);
		}

		SECTION("nullptr")
		{
			std::shared_ptr<char> p;
			if constexpr (pal::expect_noexcept)
			{
				REQUIRE_NOTHROW(pal_expect(p));
				CHECK(pal_expect(p) == nullptr);
			}
			else
			{
				REQUIRE_THROWS_AS(pal_expect(p), std::logic_error);
			}
		}
	}

	SECTION("nullptr_t")
	{
		if constexpr (pal::expect_noexcept)
		{
			REQUIRE_NOTHROW(pal_expect(nullptr));
			CHECK(pal_expect(nullptr) == nullptr);
		}
		else
		{
			REQUIRE_THROWS_AS(pal_expect(nullptr), std::logic_error);
		}
	}
}


} // namespace
