#include <pal/result>
#include <pal/test>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>


namespace {


// unexpected<T> {{{1

using trivial_type = int;
using non_trivial_type = std::string;

template <typename TestType>
TestType value ()
{
	return TestType{1};
}

template <>
std::string value ()
{
	return "1";
}

short other_value (int)
{
	return 2;
}

std::string_view other_value (std::string)
{
	return "2";
}

TEMPLATE_TEST_CASE("unexpected", "",
	trivial_type,
	non_trivial_type)
{
	const auto v1 = value<TestType>();
	const auto v2 = other_value(v1);

	SECTION("unexpected(in_place)")
	{
		pal::unexpected<TestType> e{std::in_place, v1};
		CHECK(e.error() == v1);
	}

	SECTION("unexpected(U&&)")
	{
		pal::unexpected e{v1};
		CHECK(e.error() == v1);
	}

	SECTION("unexpected(const unexpected<E> &)")
	{
		pal::unexpected that{v1};
		pal::unexpected e{that};
		CHECK(e.error() == v1);
	}

	SECTION("unexpected(unexpected<E> &&)")
	{
		pal::unexpected that{v1};
		pal::unexpected e{std::move(that)};
		CHECK(e.error() == v1);
	}

	SECTION("const error &")
	{
		pal::unexpected e{v1};
		CHECK(static_cast<const decltype(e) &>(e).error() == v1);
	}

	SECTION("error &")
	{
		pal::unexpected e{v1};
		CHECK(static_cast<decltype(e) &>(e).error() == v1);
	}

	SECTION("const error &&")
	{
		pal::unexpected e{v1};
		CHECK(std::forward<const decltype(e) &&>(e).error() == v1);
	}

	SECTION("error &&")
	{
		pal::unexpected e{v1};
		CHECK(std::forward<decltype(e) &&>(e).error() == v1);
	}

	SECTION("swap")
	{
		pal::unexpected e1{v1}, e2{static_cast<decltype(v1)>(v2)};
		CHECK(e1.error() == v1);
		CHECK(e2.error() == v2);
		e1.swap(e2);
		CHECK(e1.error() == v2);
		CHECK(e2.error() == v1);
	}

	SECTION("equal / not equal")
	{
		pal::unexpected e{v1}, e1{v1}, e2{static_cast<decltype(v1)>(v2)};
		CHECK(e == e1);
		CHECK_FALSE(e == e2);
		CHECK(e != e2);
		CHECK_FALSE(e != e1);
	}
}


// unexpected<T>::constexpr {{{1

TEST_CASE("unexpected::constexpr")
{
	using T = int;

	constexpr T value{1};

	constexpr pal::unexpected x{value};
	static_assert(x.error() == value);

	constexpr pal::unexpected<T> y{std::in_place, value};
	static_assert(y.error() == value);

	constexpr pal::unexpected w{x};
	static_assert(w.error() == value);

	constexpr pal::unexpected z{std::move(y)};
	static_assert(z.error() == value);
}


// }}}1



} // namespace
