#include <pal/result>
#include <pal/test>
#include <pal/version>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

namespace {

template <typename T>
T value ();

using trivial_type = int;
using non_trivial_type = std::string;

template <>
trivial_type value ()
{
	return 1;
}

short other_value (trivial_type)
{
	return 2;
}

template <>
non_trivial_type value ()
{
	return "1";
}

std::string_view other_value (non_trivial_type)
{
	return "2";
}

constexpr auto e = std::errc::not_enough_memory;
constexpr auto e1 = std::errc::value_too_large;
const auto u = pal::make_unexpected(e);

TEMPLATE_TEST_CASE("result", "",
	trivial_type,
	non_trivial_type)
{
	using T = pal::result<TestType>;

	const auto v = value<TestType>();
	const auto v1 = other_value(v);
	using OtherType = std::remove_cvref_t<decltype(v1)>;

	SECTION("result()") //{{{1
	{
		T x;
		CHECK(x.value() == TestType{});
	}

	SECTION("result(Args...)") //{{{1
	{
		T x{v};
		CHECK(x.value() == v);

		if constexpr (std::is_same_v<TestType, std::string>)
		{
			T y{v.begin(), v.end()};
			CHECK(y.value() == v);
		}
	}

	SECTION("result(const result &)") //{{{1
	{
		SECTION("value")
		{
			T x{v}, y{x};
			CHECK(y.value() == v);
		}

		SECTION("error")
		{
			T x{u}, y{x};
			CHECK(y.error() == e);
		}
	}

	SECTION("result(result &&)") //{{{1
	{
		SECTION("value")
		{
			T x{v}, y{std::move(x)};
			CHECK(y.value() == v);
		}

		SECTION("error")
		{
			T x{u}, y{std::move(x)};
			CHECK(y.error() == e);
		}
	}

	SECTION("result(const result<OtherType> &)") //{{{1
	{
		SECTION("value")
		{
			pal::result<OtherType> x{v1};
			T y{x};
			CHECK(y.value() == v1);
		}

		SECTION("error")
		{
			pal::result<OtherType> x{u};
			T y{x};
			CHECK(y.error() == e);
		}
	}

	SECTION("result(result<OtherType> &&)") //{{{1
	{
		SECTION("value")
		{
			pal::result<OtherType> x{v1};
			T y{std::move(x)};
			CHECK(y.value() == v1);
		}

		SECTION("error")
		{
			pal::result<OtherType> x{u};
			T y{std::move(x)};
			CHECK(y.error() == e);
		}
	}

	SECTION("result(OtherType &&)") //{{{1
	{
		T x{v1};
		CHECK(x.value() == v1);
	}

	SECTION("user defined deduction guide") //{{{1
	{
		pal::result x{v};
		static_assert(std::is_same_v<typename decltype(x)::value_type, TestType>);

		pal::result y{v1};
		static_assert(std::is_same_v<typename decltype(y)::value_type, OtherType>);
	}

	SECTION("result(const unexpected<std::error_code> &)") //{{{1
	{
		T x{u};
		CHECK(x.error() == e);
	}

	SECTION("result(unexpected<std::error_code> &&)") //{{{1
	{
		auto u1 = u;
		T x{std::move(u1)};
		CHECK(x.error() == e);
	}

	SECTION("result(std::in_place, Args...)") //{{{1
	{
		T x{std::in_place, v};
		CHECK(x.value() == v);

		if constexpr (std::is_same_v<TestType, std::string>)
		{
			T y{std::in_place, v.begin(), v.end()};
			CHECK(y.value() == v);
		}
	}

	SECTION("result(unexpect_t, Args...)") //{{{1
	{
		auto ec = std::make_error_code(e);
		T x{pal::unexpect, ec.value(), ec.category()};
		CHECK(x.error() == e);
	}

	SECTION("operator=(const result &)") //{{{1
	{
		SECTION("left && right")
		{
			T left{v1}, right{v};
			left = right;
			CHECK(left.value() == v);
		}

		SECTION("left && !right")
		{
			T left{v}, right{u};
			left = right;
			CHECK(left.error() == e);
		}

		SECTION("!left && right")
		{
			T left{u}, right{v};
			left = right;
			CHECK(left.value() == v);
		}

		SECTION("!left && !right")
		{
			T left{u}, right{u};
			left = right;
			CHECK(left.error() == e);
		}
	}

	SECTION("operator=(result &&)") //{{{1
	{
		SECTION("left && right")
		{
			T left{v1}, right{v};
			left = std::move(right);
			CHECK(left.value() == v);
		}

		SECTION("left && !right")
		{
			T left{v}, right{u};
			left = std::move(right);
			CHECK(left.error() == e);
		}

		SECTION("!left && right")
		{
			T left{u}, right{v};
			left = std::move(right);
			CHECK(left.value() == v);
		}

		SECTION("!left && !right")
		{
			T left{u}, right{u};
			left = std::move(right);
			CHECK(left.error() == e);
		}
	}

	SECTION("operator=(OtherType &&)") //{{{1
	{
		SECTION("value")
		{
			T x{v};
			x = std::move(v1);
			CHECK(x.value() == v1);
		}

		SECTION("error")
		{
			T x{u};
			x = std::move(v1);
			CHECK(x.value() == v1);
		}
	}

	SECTION("operator=(const unexpected<E> &)") //{{{1
	{
		SECTION("left")
		{
			T left{v};
			left = u;
			CHECK(left.error() == e);
		}

		SECTION("!left")
		{
			T left{u};
			left = u;
			CHECK(left.error() == e);
		}
	}

	SECTION("operator=(unexpected<E> &&)") //{{{1
	{
		auto u1 = u;

		SECTION("left")
		{
			T left{v};
			left = std::move(u1);
			CHECK(left.error() == e);
		}

		SECTION("!left")
		{
			T left{u};
			left = std::move(u1);
			CHECK(left.error() == e);
		}
	}

	SECTION("emplace") //{{{1
	{
		// emplace(std::move(v)) to work around std::string(...) noexcept(false)
		auto tmp = v;

		SECTION("value")
		{
			T x{v1};
			CHECK(x.emplace(std::move(tmp)) == v);
			CHECK(x.value() == v);
		}

		SECTION("error")
		{
			T x{u};
			CHECK(x.emplace(std::move(tmp)) == v);
			CHECK(x.value() == v);
		}
	}

	SECTION("swap") //{{{1
	{
		SECTION("left && right")
		{
			T left{v1}, right{v};
			left.swap(right);
			CHECK(right.value() == v1);
			CHECK(left.value() == v);
		}

		SECTION("left && !right")
		{
			T left{v}, right{u};
			left.swap(right);
			CHECK(left.error() == e);
			CHECK(right.value() == v);
		}

		SECTION("!left && right")
		{
			T left{u}, right{v};
			left.swap(right);
			CHECK(left.value() == v);
			CHECK(right.error() == e);
		}

		SECTION("!left && !right")
		{
			T left{u}, right{u};
			left.swap(right);
			CHECK(left.error() == e);
			CHECK(right.error() == e);
		}
	}

	SECTION("observers") //{{{1
	{
		SECTION("value")
		{
			T x{v1};

			CHECK(x);
			CHECK(x.has_value());

			CHECK(x.value() == v1);
			CHECK(static_cast<const T &>(x).value() == v1);
			CHECK(static_cast<T &&>(x).value() == v1);
			CHECK(static_cast<const T &&>(x).value() == v1);

			CHECK(*x.operator->() == v1);
			CHECK(*static_cast<const T &>(x).operator->() == v1);

			CHECK(*x == v1);
			CHECK(*static_cast<const T &>(x) == v1);

			CHECK(*static_cast<T &&>(x) == v1);
			CHECK(*static_cast<const T &&>(x) == v1);

			CHECK(x.value_or(v) == v1);
			CHECK(static_cast<T &&>(x).value_or(v) == v1);

			auto ec = std::make_error_code(e1);
			CHECK(x.error_or(ec) == e1);
			CHECK(static_cast<T &&>(x).error_or(ec) == e1);
		}

		SECTION("error")
		{
			T x{u};

			CHECK_FALSE(x);
			CHECK_FALSE(x.has_value());

			CHECK_THROWS_AS(x.value(), std::system_error);
			CHECK_THROWS_AS(static_cast<const T &>(x).value(), std::system_error);
			CHECK_THROWS_AS(static_cast<T &&>(x).value(), std::system_error);
			CHECK_THROWS_AS(static_cast<const T &&>(x).value(), std::system_error);

			CHECK(x.error() == e);
			CHECK(static_cast<const T &>(x).error() == e);
			CHECK(static_cast<T &&>(x).error() == e);
			CHECK(static_cast<const T &&>(x).error() == e);

			CHECK(x.value_or(v) == v);
			CHECK(static_cast<T &&>(x).value_or(v) == v);

			auto ec = std::make_error_code(e1);
			CHECK(x.error_or(ec) == e);
			CHECK(static_cast<T &&>(x).error_or(ec) == e);
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
				T x{v};
				CHECK(static_cast<const T &>(x).and_then(inc_count).has_value());
				CHECK(static_cast<T &>(x).and_then(inc_count).has_value());
				CHECK(static_cast<const T &&>(x).and_then(inc_count).has_value());
				CHECK(static_cast<T &&>(x).and_then(inc_count).has_value());
				CHECK(count == 4);
			}

			SECTION("error")
			{
				T x{u};
				CHECK(static_cast<const T &>(x).and_then(inc_count).error() == e);
				CHECK(static_cast<T &>(x).and_then(inc_count).error() == e);
				CHECK(static_cast<const T &&>(x).and_then(inc_count).error() == e);
				CHECK(static_cast<T &&>(x).and_then(inc_count).error() == e);
				CHECK(count == 0);
			}
		}

		SECTION("lambda with return")
		{
			auto v_to_e = [&](TestType) -> T
			{
				inc_count(e1);
				return pal::make_unexpected(e1);
			};

			SECTION("value")
			{
				T x{v};
				CHECK(static_cast<const T &>(x).and_then(v_to_e).error() == e1);
				CHECK(static_cast<T &>(x).and_then(v_to_e).error() == e1);
				CHECK(static_cast<const T &&>(x).and_then(v_to_e).error() == e1);
				CHECK(static_cast<T &&>(x).and_then(v_to_e).error() == e1);
				CHECK(count == 4);
			}

			SECTION("error")
			{
				T x{u};
				CHECK(static_cast<const T &>(x).and_then(v_to_e).error() == e);
				CHECK(static_cast<T &>(x).and_then(v_to_e).error() == e);
				CHECK(static_cast<const T &&>(x).and_then(v_to_e).error() == e);
				CHECK(static_cast<T &&>(x).and_then(v_to_e).error() == e);
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
				T x{v};
				CHECK(static_cast<const T &>(x).or_else(inc_count).value() == v);
				CHECK(static_cast<T &>(x).or_else(inc_count).value() == v);
				CHECK(static_cast<const T &&>(x).or_else(inc_count).value() == v);
				CHECK(static_cast<T &&>(x).or_else(inc_count).value() == v);
				CHECK(count == 0);
			}

			SECTION("error")
			{
				T x{u};
				CHECK(static_cast<const T &>(x).or_else(inc_count).error() == e);
				CHECK(static_cast<T &>(x).or_else(inc_count).error() == e);
				CHECK(static_cast<const T &&>(x).or_else(inc_count).error() == e);
				CHECK(static_cast<T &&>(x).or_else(inc_count).error() == e);
				CHECK(count == 4);
			}
		}

		SECTION("lambda with return")
		{
			auto e_to_v = [&](std::error_code) -> T
			{
				inc_count(v1);
				return v1;
			};

			SECTION("value")
			{
				T x{v};
				CHECK(static_cast<const T &>(x).or_else(e_to_v).value() == v);
				CHECK(static_cast<T &>(x).or_else(e_to_v).value() == v);
				CHECK(static_cast<const T &&>(x).or_else(e_to_v).value() == v);
				CHECK(static_cast<T &&>(x).or_else(e_to_v).value() == v);
				CHECK(count == 0);
			}

			SECTION("error")
			{
				T x{u};
				CHECK(static_cast<const T &>(x).or_else(e_to_v).value() == v1);
				CHECK(static_cast<T &>(x).or_else(e_to_v).value() == v1);
				CHECK(static_cast<const T &&>(x).or_else(e_to_v).value() == v1);
				CHECK(static_cast<T &&>(x).or_else(e_to_v).value() == v1);
				CHECK(count == 4);
			}
		}
	}

	SECTION("transform") //{{{1
	{
		auto t_to_u = [&v1](TestType) -> OtherType
		{
			return v1;
		};

		SECTION("TestType -> OtherType")
		{
			SECTION("value")
			{
				T x{v};
				CHECK(static_cast<const T &>(x).transform(t_to_u).value() == v1);
				CHECK(static_cast<T &>(x).transform(t_to_u).value() == v1);
				CHECK(static_cast<const T &&>(x).transform(t_to_u).value() == v1);
				CHECK(static_cast<T &&>(x).transform(t_to_u).value() == v1);
			}

			SECTION("error")
			{
				T x{u};
				CHECK(static_cast<const T &>(x).transform(t_to_u).error() == e);
				CHECK(static_cast<T &>(x).transform(t_to_u).error() == e);
				CHECK(static_cast<const T &&>(x).transform(t_to_u).error() == e);
				CHECK(static_cast<T &&>(x).transform(t_to_u).error() == e);
			}
		}
	}

	SECTION("operator==") //{{{1
	{
		// TODO: Catch2 has wires crossed here, mixing trivial_type and non_trivial_type
		#if __pal_os_macos && __pal_compiler_clang
			#define __pal_check(X) assert(X)
			#define __pal_check_false(X) assert(!(X))
		#else
			#define __pal_check(X) CHECK(X)
			#define __pal_check_false(X) CHECK_FALSE(X)
		#endif

		SECTION("left && right")
		{
			T left{v}, right{v};
			__pal_check(left == right);
			__pal_check(left == v);
			__pal_check_false(left == v1);
			__pal_check_false(left == u);

			right = v1;
			__pal_check_false(left == right);
		}

		SECTION("left && !right")
		{
			T left{v}, right{u};
			__pal_check_false(left == right);
			__pal_check(left == v);
			__pal_check_false(left == u);
		}

		SECTION("!left && right")
		{
			T left{u}, right{v};
			__pal_check_false(left == right);
			__pal_check_false(left == v);
			__pal_check(left == u);
		}

		SECTION("!left && !right")
		{
			T left{u}, right{u};
			__pal_check(left == right);
			__pal_check_false(left == v);
			__pal_check(left == u);

			auto u1 = pal::make_unexpected(e1);
			__pal_check_false(left == u1);

			right = u1;
			__pal_check_false(left == right);
		}

		#undef __pal_check
		#undef __pal_check_false
	}

	//}}}1
}

TEMPLATE_TEST_CASE("result", "",
	void)
{
	using T = pal::result<void>;

	SECTION("result()") //{{{1
	{
		T x;
		CHECK(x.has_value());
	}

	SECTION("result(const result &)") //{{{1
	{
		SECTION("value")
		{
			T x, y{x};
			CHECK(y.has_value());
		}

		SECTION("error")
		{
			T x{u}, y{x};
			CHECK(y.error() == e);
		}
	}

	SECTION("result(result &&)") //{{{1
	{
		SECTION("value")
		{
			T x, y{std::move(x)};
			CHECK(y.has_value());
		}

		SECTION("error")
		{
			T x{u}, y{std::move(x)};
			CHECK(y.error() == e);
		}
	}

	SECTION("result(const unexpected<std::error_code> &)") //{{{1
	{
		T x{u};
		CHECK(x.error() == e);
	}

	SECTION("result(unexpected<std::error_code> &&)") //{{{1
	{
		auto u1 = u;
		T x{std::move(u1)};
		CHECK(x.error() == e);
	}

	SECTION("result(std::in_place)") //{{{1
	{
		T x{std::in_place};
		CHECK(x.has_value());
	}

	SECTION("result(unexpect_t, Args...)") //{{{1
	{
		auto ec = std::make_error_code(e);
		T x{pal::unexpect, ec.value(), ec.category()};
		CHECK(x.error() == e);
	}

	SECTION("operator=(const result &)") //{{{1
	{
		SECTION("left && right")
		{
			T left, right;
			left = right;
			CHECK(left.has_value());
		}

		SECTION("left && !right")
		{
			T left, right{u};
			left = right;
			CHECK(left.error() == e);
		}

		SECTION("!left && right")
		{
			T left{u}, right;
			left = right;
			CHECK(left.has_value());
		}

		SECTION("!left && !right")
		{
			T left{u}, right{u};
			left = right;
			CHECK(left.error() == e);
		}
	}

	SECTION("operator=(result &&)") //{{{1
	{
		SECTION("left && right")
		{
			T left, right;
			left = std::move(right);
			CHECK(left.has_value());
		}

		SECTION("left && !right")
		{
			T left, right{u};
			left = std::move(right);
			CHECK(left.error() == e);
		}

		SECTION("!left && right")
		{
			T left{u}, right;
			left = std::move(right);
			CHECK(left.has_value());
		}

		SECTION("!left && !right")
		{
			T left{u}, right{u};
			left = std::move(right);
			CHECK(left.error() == e);
		}
	}

	SECTION("operator=(const unexpected<E> &)") //{{{1
	{
		SECTION("left")
		{
			T left;
			left = u;
			CHECK(left.error() == e);
		}

		SECTION("!left")
		{
			T left{u};
			left = u;
			CHECK(left.error() == e);
		}
	}

	SECTION("operator=(unexpected<E> &&)") //{{{1
	{
		auto u1 = u;

		SECTION("left")
		{
			T left;
			left = std::move(u1);
			CHECK(left.error() == e);
		}

		SECTION("!left")
		{
			T left{u};
			left = std::move(u1);
			CHECK(left.error() == e);
		}
	}

	SECTION("emplace") //{{{1
	{
		SECTION("value")
		{
			T x;
			x.emplace();
			CHECK(x.has_value());
		}

		SECTION("error")
		{
			T x{u};
			x.emplace();
			CHECK(x.has_value());
		}
	}

	SECTION("swap") //{{{1
	{
		SECTION("left && right")
		{
			T left, right;
			left.swap(right);
			CHECK(right.has_value());
			CHECK(left.has_value());
		}

		SECTION("left && !right")
		{
			T left, right{u};
			left.swap(right);
			CHECK(left.error() == e);
			CHECK(right.has_value());
		}

		SECTION("!left && right")
		{
			T left{u}, right;
			left.swap(right);
			CHECK(left.has_value());
			CHECK(right.error() == e);
		}

		SECTION("!left && !right")
		{
			T left{u}, right{u};
			left.swap(right);
			CHECK(left.error() == e);
			CHECK(right.error() == e);
		}
	}

	SECTION("observers") //{{{1
	{
		SECTION("value")
		{
			T x;

			CHECK(x);
			CHECK(x.has_value());

			CHECK_NOTHROW(x.value());
			CHECK_NOTHROW(static_cast<T &&>(x).value());

			CHECK_NOTHROW(*x);

			auto ec = std::make_error_code(e1);
			CHECK(x.error_or(ec) == e1);
			CHECK(static_cast<T &&>(x).error_or(ec) == e1);
		}

		SECTION("error")
		{
			T x{u};

			CHECK_FALSE(x);
			CHECK_FALSE(x.has_value());

			CHECK_THROWS_AS(x.value(), std::system_error);
			CHECK_THROWS_AS(static_cast<T &&>(x).value(), std::system_error);

			CHECK(x.error() == e);
			CHECK(static_cast<const T &>(x).error() == e);
			CHECK(static_cast<T &&>(x).error() == e);
			CHECK(static_cast<const T &&>(x).error() == e);

			auto ec = std::make_error_code(e1);
			CHECK(x.error_or(ec) == e);
			CHECK(static_cast<T &&>(x).error_or(ec) == e);
		}
	}

	int count = 0;

	auto inc_count = [&count]()
	{
		count++;
	};

	SECTION("and_then") //{{{1
	{
		SECTION("lambda with no return")
		{
			SECTION("value")
			{
				T x;
				CHECK(static_cast<const T &>(x).and_then(inc_count).has_value());
				CHECK(static_cast<T &>(x).and_then(inc_count).has_value());
				CHECK(static_cast<const T &&>(x).and_then(inc_count).has_value());
				CHECK(static_cast<T &&>(x).and_then(inc_count).has_value());
				CHECK(count == 4);
			}

			SECTION("error")
			{
				T x{u};
				CHECK(static_cast<const T &>(x).and_then(inc_count).error() == e);
				CHECK(static_cast<T &>(x).and_then(inc_count).error() == e);
				CHECK(static_cast<const T &&>(x).and_then(inc_count).error() == e);
				CHECK(static_cast<T &&>(x).and_then(inc_count).error() == e);
				CHECK(count == 0);
			}
		}

		SECTION("lambda with return")
		{
			auto v_to_e = [&]() -> T
			{
				inc_count();
				return pal::make_unexpected(e1);
			};

			SECTION("value")
			{
				T x;
				CHECK(static_cast<const T &>(x).and_then(v_to_e).error() == e1);
				CHECK(static_cast<T &>(x).and_then(v_to_e).error() == e1);
				CHECK(static_cast<const T &&>(x).and_then(v_to_e).error() == e1);
				CHECK(static_cast<T &&>(x).and_then(v_to_e).error() == e1);
				CHECK(count == 4);
			}

			SECTION("error")
			{
				T x{u};
				CHECK(static_cast<const T &>(x).and_then(v_to_e).error() == e);
				CHECK(static_cast<T &>(x).and_then(v_to_e).error() == e);
				CHECK(static_cast<const T &&>(x).and_then(v_to_e).error() == e);
				CHECK(static_cast<T &&>(x).and_then(v_to_e).error() == e);
				CHECK(count == 0);
			}
		}
	}

	SECTION("or_else") //{{{1
	{
		SECTION("lambda with no return")
		{
			auto handle_e = [&](std::error_code)
			{
				inc_count();
			};

			SECTION("value")
			{
				T x;
				CHECK(static_cast<const T &>(x).or_else(handle_e).has_value());
				CHECK(static_cast<T &>(x).or_else(handle_e).has_value());
				CHECK(static_cast<const T &&>(x).or_else(handle_e).has_value());
				CHECK(static_cast<T &&>(x).or_else(handle_e).has_value());
				CHECK(count == 0);
			}

			SECTION("error")
			{
				T x{u};
				CHECK(static_cast<const T &>(x).or_else(handle_e).error() == e);
				CHECK(static_cast<T &>(x).or_else(handle_e).error() == e);
				CHECK(static_cast<const T &&>(x).or_else(handle_e).error() == e);
				CHECK(static_cast<T &&>(x).or_else(handle_e).error() == e);
				CHECK(count == 4);
			}
		}

		SECTION("lambda with return")
		{
			auto e_to_v = [&](std::error_code) -> T
			{
				inc_count();
				return {};
			};

			SECTION("value")
			{
				T x;
				CHECK(static_cast<const T &>(x).or_else(e_to_v).has_value());
				CHECK(static_cast<T &>(x).or_else(e_to_v).has_value());
				CHECK(static_cast<const T &&>(x).or_else(e_to_v).has_value());
				CHECK(static_cast<T &&>(x).or_else(e_to_v).has_value());
				CHECK(count == 0);
			}

			SECTION("error")
			{
				T x{u};
				CHECK(static_cast<const T &>(x).or_else(e_to_v).has_value());
				CHECK(static_cast<T &>(x).or_else(e_to_v).has_value());
				CHECK(static_cast<const T &&>(x).or_else(e_to_v).has_value());
				CHECK(static_cast<T &&>(x).or_else(e_to_v).has_value());
				CHECK(count == 4);
			}
		}
	}

	SECTION("transform") //{{{1
	{
		auto void_to_int = []() -> int
		{
			return 1;
		};

		SECTION("void -> int")
		{
			SECTION("value")
			{
				T x;
				CHECK(static_cast<const T &>(x).transform(void_to_int).value() == 1);
				CHECK(static_cast<T &>(x).transform(void_to_int).value() == 1);
				CHECK(static_cast<const T &&>(x).transform(void_to_int).value() == 1);
				CHECK(static_cast<T &&>(x).transform(void_to_int).value() == 1);
			}

			SECTION("error")
			{
				T x{u};
				CHECK(static_cast<const T &>(x).transform(void_to_int).error() == e);
				CHECK(static_cast<T &>(x).transform(void_to_int).error() == e);
				CHECK(static_cast<const T &&>(x).transform(void_to_int).error() == e);
				CHECK(static_cast<T &&>(x).transform(void_to_int).error() == e);
			}
		}
	}

	SECTION("operator==") //{{{1
	{
		SECTION("left && right")
		{
			T left, right;
			CHECK(left == right);
			CHECK_FALSE(left == u);
		}

		SECTION("left && !right")
		{
			T left, right{u};
			CHECK_FALSE(left == right);
			CHECK_FALSE(left == u);
		}

		SECTION("!left && right")
		{
			T left{u}, right;
			CHECK_FALSE(left == right);
			CHECK(left == u);
		}

		SECTION("!left && !right")
		{
			T left{u}, right{u};
			CHECK(left == right);

			auto u1 = pal::make_unexpected(e1);
			CHECK_FALSE(left == u1);

			right = u1;
			CHECK_FALSE(left == right);
		}
	}

	//}}}1
}

struct no_noexcept_type
{
	static inline bool do_throw = false;

	static void check_throw () noexcept(false)
	{
		if (do_throw)
		{
			do_throw = false;
			throw true;
		}
	}

	no_noexcept_type () = default;

	no_noexcept_type (const no_noexcept_type &) noexcept(false)
	{
		check_throw();
	}

	no_noexcept_type (no_noexcept_type &&) noexcept(false)
	{
		check_throw();
	}

	no_noexcept_type &operator= (const no_noexcept_type &) noexcept(false)
	{
		check_throw();
		return *this;
	}

	no_noexcept_type &operator= (no_noexcept_type &&) noexcept(false)
	{
		check_throw();
		return *this;
	}
};

TEMPLATE_TEST_CASE("result", "",
	no_noexcept_type)
{
	TestType v;

	SECTION("no throw")
	{
		SECTION("value")
		{
			pal::result<TestType> x;
			CHECK_NOTHROW(x = v);
			CHECK(x.has_value());
		}

		SECTION("error")
		{
			pal::result<TestType> x{u};
			CHECK_NOTHROW(x = v);
			CHECK(x.has_value());
		}
	}

	SECTION("throw")
	{
		v.do_throw = true;

		SECTION("value")
		{
			pal::result<TestType> x;
			CHECK_THROWS_AS(x = v, bool);
			CHECK(x.has_value());
		}

		SECTION("error")
		{
			pal::result<TestType> x{u};
			CHECK_THROWS_AS(x = v, bool);
			REQUIRE_FALSE(x.has_value());
			CHECK(x.error() == e);
		}
	}

}

} // namespace
