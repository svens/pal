#include <pal/require.hpp>
#include <pal/test.hpp>
#include <catch2/catch_test_macros.hpp>
#include <memory>

namespace
{

TEST_CASE("require")
{
	SECTION("pass-through: bool")
	{
		auto v = pal_require(true);
		CHECK(v == true);
	}

	SECTION("pass-through: bool with message")
	{
		auto v = pal_require(true, "must hold");
		CHECK(v == true);
	}

	SECTION("pass-through: integral value")
	{
		auto v = pal_require(42);
		CHECK(v == 42);
	}

	SECTION("pass-through: raw pointer")
	{
		int x = 1;
		auto *p = pal_require(&x);
		CHECK(p == &x);
	}

	SECTION("pass-through: move-only value")
	{
		auto owned = std::make_unique<int>(7);
		auto v = pal_require(std::move(owned));
		REQUIRE(v);
		CHECK(*v == 7);
	}

	SECTION("pass-through: lvalue is returned by reference")
	{
		auto owned = std::make_unique<int>(9);
		auto &ref = pal_require(owned);
		CHECK(&ref == &owned);
	}

	SECTION("expression is always evaluated")
	{
		int calls = 0;
		auto side_effect = [&calls] ()
		{
			++calls;
			return true;
		};
		auto v = pal_require(side_effect());
		CHECK(v == true);
		CHECK(calls == 1);
	}

	SECTION("constexpr-compatible")
	{
		constexpr auto v = pal_require(42);
		static_assert(v == 42);
		CHECK(v == 42);
	}

	SECTION("failure path")
	{
		if constexpr (pal::build == pal::build_type::debug)
		{
			auto msg = pal_test::require_terminate([&] { pal_require(false); });
			CHECK(msg.contains("'false'"));

			msg = pal_test::require_terminate([&] { pal_require(false, "must not be false"); });
			CHECK(msg.contains("must not be false"));

			int *null_ptr = nullptr;
			msg = pal_test::require_terminate([&] { pal_require(null_ptr); });
			CHECK(msg.contains("'null_ptr == nullptr'"));
		}
		else
		{
			// enforcement is debug-only: a falsy release-build pal_require
			// still evaluates and passes the value through, never terminates
			auto v = pal_require(false);
			CHECK(v == false);
		}
	}
}

} // namespace
