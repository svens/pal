#include <pal/expected>
#include <pal/test>


namespace {


TEMPLATE_TEST_CASE("unexpected", "", uint8_t, uint64_t)
{
	using OtherType = bool;

	SECTION("inplace ctor")
	{
		TestType value{};
		pal::unexpected<TestType> e{std::in_place, value};
	}

	SECTION("rebind move ctor")
	{
		TestType value{};
		pal::unexpected e{std::move(value)};
	}

	SECTION("rebind copy ctor")
	{
		pal::unexpected<OtherType> that{OtherType{}};
		pal::unexpected<TestType> e{that};
	}

	SECTION("rebind move ctor")
	{
		pal::unexpected<OtherType> that{OtherType{}};
		pal::unexpected<TestType> e{std::move(that)};
	}

	SECTION("rebind copy assign")
	{
		pal::unexpected<OtherType> that{OtherType{}};
		pal::unexpected<TestType> e{TestType{}};
		e = that;
	}

	SECTION("rebind move assign")
	{
		pal::unexpected<OtherType> that{OtherType{}};
		pal::unexpected<TestType> e{TestType{}};
		e = std::move(that);
	}

	SECTION("const value &")
	{
		TestType v{};
		pal::unexpected<TestType> e{v};
		CHECK(static_cast<const decltype(e) &>(e).value() == v);
	}

	SECTION("value &")
	{
		TestType v{};
		pal::unexpected<TestType> e{v};
		CHECK(static_cast<decltype(e) &>(e).value() == v);
	}

	SECTION("const value &&")
	{
		TestType v{};
		pal::unexpected<TestType> e{v};
		CHECK(std::forward<const decltype(e) &&>(e).value() == v);
	}

	SECTION("value &&")
	{
		TestType v{};
		pal::unexpected<TestType> e{v};
		CHECK(std::forward<decltype(e) &&>(e).value() == v);
	}

	SECTION("swap")
	{
		TestType v1{}, v2 = v1 + 1;
		pal::unexpected<TestType> e1{v1}, e2{v2};
		CHECK(e1.value() == v1);
		CHECK(e2.value() == v2);
		e1.swap(e2);
		CHECK(e1.value() == v2);
		CHECK(e2.value() == v1);

		pal::swap(e1, e2);
		CHECK(e1.value() == v1);
		CHECK(e2.value() == v2);
	}

	SECTION("equal / not equal")
	{
		TestType v1{}, v2 = v1 + 1;
		pal::unexpected<TestType> e{v1}, e1{v1}, e2{v2};
		CHECK(e == e1);
		CHECK_FALSE(e == e2);
		CHECK(e != e2);
		CHECK_FALSE(e != e1);
	}
}


} // namespace
