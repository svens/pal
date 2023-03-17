#include <pal/result>
#include <pal/test>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

namespace {

using trivial_type = pal::result<int>;
using non_trivial_type = pal::result<std::string>;

template <typename T>
T value ()
{
	return T{1};
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

TEMPLATE_TEST_CASE("result", "",
	trivial_type,
	non_trivial_type)
{
	using T = typename TestType::value_type;
	const auto v1 = value<T>();

	const auto v2 = other_value(v1);
	using U = std::remove_cvref_t<decltype(v2)>;

	constexpr auto e = std::errc::not_enough_memory;
	constexpr auto e1 = std::errc::value_too_large;
	const auto u = pal::make_unexpected(e);

	SECTION("result()") //{{{1
	{
		TestType x;
		CHECK(x.value() == T{});
	}

	SECTION("result(Args...)") //{{{1
	{
		TestType x{v1};
		CHECK(x.value() == v1);

		if constexpr (std::is_same_v<T, std::string>)
		{
			TestType y{v1.begin(), v1.end()};
			CHECK(y.value() == v1);
		}
	}

	SECTION("result(const result &)") //{{{1
	{
		SECTION("value")
		{
			TestType x{v1}, y{x};
			CHECK(y.value() == v1);
		}

		SECTION("error")
		{
			TestType x{u}, y{x};
			CHECK(y.error() == e);
		}
	}

	SECTION("result(result &&)") //{{{1
	{
		SECTION("value")
		{
			TestType x{v1}, y{std::move(x)};
			CHECK(y.value() == v1);
		}

		SECTION("error")
		{
			TestType x{u}, y{std::move(x)};
			CHECK(y.error() == e);
		}
	}

	SECTION("result(const result<U> &)") //{{{1
	{
		SECTION("value")
		{
			pal::result<U> x{v2};
			TestType y{x};
			CHECK(y.value() == v2);
		}

		SECTION("error")
		{
			pal::result<U> x{u};
			TestType y{x};
			CHECK(y.error() == e);
		}
	}

	SECTION("result(result<U> &&)") //{{{1
	{
		SECTION("value")
		{
			pal::result<U> x{v2};
			TestType y{std::move(x)};
			CHECK(y.value() == v2);
		}

		SECTION("error")
		{
			pal::result<U> x{u};
			TestType y{std::move(x)};
			CHECK(y.error() == e);
		}
	}

	SECTION("result(U &&)") //{{{1
	{
		TestType x{v2};
		CHECK(x.value() == v2);
	}

	SECTION("user defined deduction guide") //{{{1
	{
		pal::result x{v1};
		static_assert(std::is_same_v<typename decltype(x)::value_type, T>);

		pal::result y{v2};
		static_assert(std::is_same_v<typename decltype(y)::value_type, U>);
	}

	SECTION("result(const unexpected<std::error_code> &)") //{{{1
	{
		TestType x{u};
		CHECK(x.error() == e);
	}

	SECTION("result(unexpected<std::error_code> &&)") //{{{1
	{
		TestType x{std::move(u)};
		CHECK(x.error() == e);
	}

	SECTION("result(std::in_place, Args...)") //{{{1
	{
		TestType x{std::in_place, v1};
		CHECK(x.value() == v1);

		if constexpr (std::is_same_v<T, std::string>)
		{
			TestType y{std::in_place, v1.begin(), v1.end()};
			CHECK(y.value() == v1);
		}
	}

	SECTION("result(unexpect_t, Args...)") //{{{1
	{
		auto ec = std::make_error_code(e);
		TestType x{pal::unexpect, ec.value(), ec.category()};
		CHECK(x.error() == e);
	}

	SECTION("operator=(const result &)") //{{{1
	{
		SECTION("left && right")
		{
			TestType left{v2}, right{v1};
			left = right;
			CHECK(left.value() == v1);
		}

		SECTION("left && !right")
		{
			TestType left{v1}, right{u};
			left = right;
			CHECK(left.error() == e);
		}

		SECTION("!left && right")
		{
			TestType left{u}, right{v1};
			left = right;
			CHECK(left.value() == v1);
		}

		SECTION("!left && !right")
		{
			TestType left{u}, right{u};
			left = right;
			CHECK(left.error() == e);
		}
	}

	SECTION("operator=(result &&)") //{{{1
	{
		SECTION("left && right")
		{
			TestType left{v2}, right{v1};
			left = std::move(right);
			CHECK(left.value() == v1);
		}

		SECTION("left && !right")
		{
			TestType left{v1}, right{u};
			left = std::move(right);
			CHECK(left.error() == e);
		}

		SECTION("!left && right")
		{
			TestType left{u}, right{v1};
			left = std::move(right);
			CHECK(left.value() == v1);
		}

		SECTION("!left && !right")
		{
			TestType left{u}, right{u};
			left = std::move(right);
			CHECK(left.error() == e);
		}
	}

	SECTION("operator=(U &&)") //{{{1
	{
		SECTION("value")
		{
			TestType x{v1};
			x = v2;
			CHECK(x.value() == v2);
		}

		SECTION("error")
		{
			TestType x{u};
			x = v2;
			CHECK(x.value() == v2);
		}
	}

	SECTION("operator=(const unexpected<E> &)") //{{{1
	{
		SECTION("left")
		{
			TestType left{v1};
			left = u;
			CHECK(left.error() == e);
		}

		SECTION("!left")
		{
			TestType left{u};
			left = u;
			CHECK(left.error() == e);
		}
	}

	SECTION("operator=(unexpected<E> &&)") //{{{1
	{
		auto u1 = u;

		SECTION("left")
		{
			TestType left{v1};
			left = std::move(u1);
			CHECK(left.error() == e);
		}

		SECTION("!left")
		{
			TestType left{u};
			left = std::move(u1);
			CHECK(left.error() == e);
		}
	}

	SECTION("emplace") //{{{1
	{
		// emplace(std::move(v)) to work around std::string(...) noexcept(false)
		auto v = v1;

		SECTION("value")
		{
			TestType x{v2};
			CHECK(x.emplace(std::move(v)) == v1);
			CHECK(x.value() == v1);
		}

		SECTION("error")
		{
			TestType x{u};
			CHECK(x.emplace(std::move(v)) == v1);
			CHECK(x.value() == v1);
		}
	}

	SECTION("swap") //{{{1
	{
		SECTION("left && right")
		{
			TestType left{v2}, right{v1};
			left.swap(right);
			CHECK(right.value() == v2);
			CHECK(left.value() == v1);
		}

		SECTION("left && !right")
		{
			TestType left{v1}, right{u};
			left.swap(right);
			CHECK(left.error() == e);
			CHECK(right.value() == v1);
		}

		SECTION("!left && right")
		{
			TestType left{u}, right{v1};
			left.swap(right);
			CHECK(left.value() == v1);
			CHECK(right.error() == e);
		}

		SECTION("!left && !right")
		{
			TestType left{u}, right{u};
			left.swap(right);
			CHECK(left.error() == e);
			CHECK(right.error() == e);
		}
	}

	SECTION("observers") //{{{1
	{
		SECTION("value")
		{
			TestType x{v2};

			CHECK(x);
			CHECK(x.has_value());

			CHECK(x.value() == v2);
			CHECK(static_cast<const TestType &>(x).value() == v2);
			CHECK(static_cast<TestType &&>(x).value() == v2);
			CHECK(static_cast<const TestType &&>(x).value() == v2);

			CHECK(*x.operator->() == v2);
			CHECK(*static_cast<const TestType &>(x).operator->() == v2);

			CHECK(*x == v2);
			CHECK(*static_cast<const TestType &>(x) == v2);

			CHECK(*static_cast<TestType &&>(x) == v2);
			CHECK(*static_cast<const TestType &&>(x) == v2);

			CHECK(x.value_or(v1) == v2);
			CHECK(static_cast<TestType &&>(x).value_or(v1) == v2);
		}

		SECTION("error")
		{
			TestType x{u};

			CHECK_FALSE(x);
			CHECK_FALSE(x.has_value());

			CHECK_THROWS_AS(x.value(), std::system_error);
			CHECK_THROWS_AS(static_cast<const TestType &>(x).value(), std::system_error);
			CHECK_THROWS_AS(static_cast<TestType &&>(x).value(), std::system_error);
			CHECK_THROWS_AS(static_cast<const TestType &&>(x).value(), std::system_error);

			CHECK(x.error() == e);
			CHECK(static_cast<const TestType &>(x).error() == e);
			CHECK(static_cast<TestType &&>(x).error() == e);
			CHECK(static_cast<const TestType &&>(x).error() == e);

			CHECK(x.value_or(v1) == v1);
			CHECK(static_cast<TestType &&>(x).value_or(v1) == v1);
		}
	}

	SECTION("operator==") //{{{1
	{
		SECTION("left && right")
		{
			TestType left{v1}, right{v1};
			CHECK(left == right);
			CHECK(left == v1);
			CHECK_FALSE(left == v2);
			CHECK_FALSE(left == u);

			right = v2;
			CHECK_FALSE(left == right);
		}

		SECTION("left && !right")
		{
			TestType left{v1}, right{u};
			CHECK_FALSE(left == right);
			CHECK(left == v1);
			CHECK_FALSE(left == u);
		}

		SECTION("!left && right")
		{
			TestType left{u}, right{v1};
			CHECK_FALSE(left == right);
			CHECK_FALSE(left == v1);
			CHECK(left == u);
		}

		SECTION("!left && !right")
		{
			TestType left{u}, right{u};
			CHECK(left == right);
			CHECK_FALSE(left == v1);
			CHECK(left == u);

			auto u1 = pal::make_unexpected(e1);
			CHECK_FALSE(left == u1);

			right = u1;
			CHECK_FALSE(left == right);
		}
	}

	int count = 0;

	auto inc_count = [&count](auto)
	{
		count++;
	};

	SECTION("and_then") //{{{1
	{
		SECTION("lambda with no return")
		{
			SECTION("value")
			{
				TestType x{v1};
				CHECK(static_cast<const TestType &>(x).and_then(inc_count).has_value());
				CHECK(static_cast<TestType &>(x).and_then(inc_count).has_value());
				CHECK(static_cast<const TestType &&>(x).and_then(inc_count).has_value());
				CHECK(static_cast<TestType &&>(x).and_then(inc_count).has_value());
				CHECK(count == 4);
			}

			SECTION("error")
			{
				TestType x{u};
				CHECK(static_cast<const TestType &>(x).and_then(inc_count).error() == e);
				CHECK(static_cast<TestType &>(x).and_then(inc_count).error() == e);
				CHECK(static_cast<const TestType &&>(x).and_then(inc_count).error() == e);
				CHECK(static_cast<TestType &&>(x).and_then(inc_count).error() == e);
				CHECK(count == 0);
			}
		}

		SECTION("lambda with return")
		{
			auto v_to_e = [&](T v) -> TestType
			{
				inc_count(v);
				return pal::make_unexpected(e1);
			};

			SECTION("value")
			{
				TestType x{v1};
				CHECK(static_cast<const TestType &>(x).and_then(v_to_e).error() == e1);
				CHECK(static_cast<TestType &>(x).and_then(v_to_e).error() == e1);
				CHECK(static_cast<const TestType &&>(x).and_then(v_to_e).error() == e1);
				CHECK(static_cast<TestType &&>(x).and_then(v_to_e).error() == e1);
				CHECK(count == 4);
			}

			SECTION("error")
			{
				TestType x{u};
				CHECK(static_cast<const TestType &>(x).and_then(v_to_e).error() == e);
				CHECK(static_cast<TestType &>(x).and_then(v_to_e).error() == e);
				CHECK(static_cast<const TestType &&>(x).and_then(v_to_e).error() == e);
				CHECK(static_cast<TestType &&>(x).and_then(v_to_e).error() == e);
				CHECK(count == 0);
			}
		}
	}

	SECTION("or_else") //{{{1
	{
		SECTION("lambda with no return")
		{
			SECTION("value")
			{
				TestType x{v1};
				CHECK(static_cast<const TestType &>(x).or_else(inc_count).value() == v1);
				CHECK(static_cast<TestType &>(x).or_else(inc_count).value() == v1);
				CHECK(static_cast<const TestType &&>(x).or_else(inc_count).value() == v1);
				CHECK(static_cast<TestType &&>(x).or_else(inc_count).value() == v1);
				CHECK(count == 0);
			}

			SECTION("error")
			{
				TestType x{u};
				CHECK(static_cast<const TestType &>(x).or_else(inc_count).error() == e);
				CHECK(static_cast<TestType &>(x).or_else(inc_count).error() == e);
				CHECK(static_cast<const TestType &&>(x).or_else(inc_count).error() == e);
				CHECK(static_cast<TestType &&>(x).or_else(inc_count).error() == e);
				CHECK(count == 4);
			}
		}

		SECTION("lambda with return")
		{
			auto e_to_v = [&](std::error_code e) -> TestType
			{
				inc_count(e);
				return v2;
			};

			SECTION("value")
			{
				TestType x{v1};
				CHECK(static_cast<const TestType &>(x).or_else(e_to_v).value() == v1);
				CHECK(static_cast<TestType &>(x).or_else(e_to_v).value() == v1);
				CHECK(static_cast<const TestType &&>(x).or_else(e_to_v).value() == v1);
				CHECK(static_cast<TestType &&>(x).or_else(e_to_v).value() == v1);
				CHECK(count == 0);
			}

			SECTION("error")
			{
				TestType x{u};
				CHECK(static_cast<const TestType &>(x).or_else(e_to_v).value() == v2);
				CHECK(static_cast<TestType &>(x).or_else(e_to_v).value() == v2);
				CHECK(static_cast<const TestType &&>(x).or_else(e_to_v).value() == v2);
				CHECK(static_cast<TestType &&>(x).or_else(e_to_v).value() == v2);
				CHECK(count == 4);
			}
		}
	}

	SECTION("transform") //{{{1
	{
		auto t_to_u = [&v2](T) -> U
		{
			return v2;
		};

		SECTION("T -> U")
		{
			SECTION("value")
			{
				TestType x{v1};
				CHECK(static_cast<const TestType &>(x).transform(t_to_u).value() == v2);
				CHECK(static_cast<TestType &>(x).transform(t_to_u).value() == v2);
				CHECK(static_cast<const TestType &&>(x).transform(t_to_u).value() == v2);
				CHECK(static_cast<TestType &&>(x).transform(t_to_u).value() == v2);
			}

			SECTION("error")
			{
				TestType x{u};
				CHECK(static_cast<const TestType &>(x).transform(t_to_u).error() == e);
				CHECK(static_cast<TestType &>(x).transform(t_to_u).error() == e);
				CHECK(static_cast<const TestType &&>(x).transform(t_to_u).error() == e);
				CHECK(static_cast<TestType &&>(x).transform(t_to_u).error() == e);
			}
		}
	}

	//}}}1
}

} // namespace
