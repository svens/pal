#include <pal/assert>
#include <pal/test>
#include <memory>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>


namespace {


bool bool_fn (bool &side_effect, bool return_value)
{
	side_effect = true;
	return return_value;
}


TEST_CASE("assert")
{
	SECTION("bool")
	{
		bool side_effect = false;

		SECTION("true")
		{
			REQUIRE_NOTHROW(pal_assert(bool_fn(side_effect, true)));
			CHECK(pal_assert(bool_fn(side_effect, true)) == true);
		}

		SECTION("false")
		{
			if constexpr (pal::assert_noexcept)
			{
				REQUIRE_NOTHROW(pal_assert(bool_fn(side_effect, false)));
				CHECK(pal_assert(bool_fn(side_effect, false)) == false);
			}
			else
			{
				REQUIRE_THROWS_AS(
					pal_assert(bool_fn(side_effect, false)),
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
			char buf = 'a', *p = &buf;
			REQUIRE_NOTHROW(pal_assert(p));
			CHECK(pal_assert(p) == &buf);
		}

		SECTION("nullptr")
		{
			const char *p = nullptr;
			if constexpr (pal::assert_noexcept)
			{
				REQUIRE_NOTHROW(pal_assert(p));
				CHECK(pal_assert(p) == nullptr);
			}
			else
			{
				REQUIRE_THROWS_AS(pal_assert(p), std::logic_error);
			}
		}
	}

	SECTION("unique_ptr")
	{
		SECTION("not_nullptr")
		{
			auto buf = new char;
			std::unique_ptr<char> p{buf};
			REQUIRE_NOTHROW(pal_assert(p));
			CHECK(pal_assert(p).get() == buf);
		}

		SECTION("nullptr")
		{
			std::unique_ptr<char> p;
			if constexpr (pal::assert_noexcept)
			{
				REQUIRE_NOTHROW(pal_assert(p));
				CHECK(pal_assert(p) == nullptr);
			}
			else
			{
				REQUIRE_THROWS_AS(pal_assert(p), std::logic_error);
			}
		}
	}

	SECTION("shared_ptr")
	{
		SECTION("not_nullptr")
		{
			auto buf = new char;
			std::shared_ptr<char> p{buf};
			REQUIRE_NOTHROW(pal_assert(p));
			CHECK(pal_assert(p).get() == buf);
		}

		SECTION("nullptr")
		{
			std::shared_ptr<char> p;
			if constexpr (pal::assert_noexcept)
			{
				REQUIRE_NOTHROW(pal_assert(p));
				CHECK(pal_assert(p) == nullptr);
			}
			else
			{
				REQUIRE_THROWS_AS(pal_assert(p), std::logic_error);
			}
		}
	}

	SECTION("nullptr_t")
	{
		if constexpr (pal::assert_noexcept)
		{
			REQUIRE_NOTHROW(pal_assert(nullptr));
			CHECK(pal_assert(nullptr) == nullptr);
		}
		else
		{
			REQUIRE_THROWS_AS(pal_assert(nullptr), std::logic_error);
		}
	}

	if constexpr (!pal::assert_noexcept)
	{
		using Catch::Matchers::ContainsSubstring;

		SECTION("without message")
		{
			try
			{
				pal_assert(1 > 2);
				FAIL("unexpected");
			}
			catch (const std::logic_error &e)
			{
				CHECK_THAT(e.what(), ContainsSubstring("1 > 2"));
			}
		}

		SECTION("with message")
		{
			constexpr char message[] = "optional message";
			try
			{
				pal_assert(1 > 2, message);
				FAIL("unexpected");
			}
			catch (const std::logic_error &e)
			{
				CHECK_THAT(e.what(), ContainsSubstring(message));
			}
		}
	}
}


} // namespace
