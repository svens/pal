#if defined(_MSC_BUILD) && !defined(_DEBUG)
	// With throwable<> class below some intentional test-throws make
	// parts of code unreachable for MSVC Release build
	// As it is test-specific, no point to fix.
	#pragma warning(push)
	#pragma warning(disable: 4702)
#endif

#include <pal/expected>
#include <pal/test>


namespace {


struct copy_and_move
{
	int base{1};

	copy_and_move () = default;
	~copy_and_move () = default;

	copy_and_move (const copy_and_move &) = default;
	copy_and_move (copy_and_move &&) = default;
	copy_and_move &operator= (const copy_and_move &) = default;
	copy_and_move &operator= (copy_and_move &&) = default;

	copy_and_move (int v) noexcept
		: base{v}
	{}

	copy_and_move &operator= (int v) noexcept
	{
		base = v;
		return *this;
	}

	int fn () const & noexcept
	{
		return 1 * base;
	}

	int fn () & noexcept
	{
		return 2 * base;
	}

	int fn () const && noexcept
	{
		return 3 * base;
	}

	int fn () && noexcept
	{
		return 4 * base;
	}
};


struct copy_only: public copy_and_move
{
	using copy_and_move::copy_and_move;
	using copy_and_move::operator=;

	copy_only (const copy_only &) = default;
	copy_only &operator= (const copy_only &) = default;

	copy_only (copy_only &&) = delete;
	copy_only &operator= (copy_only &&) = delete;
};


struct move_only: public copy_and_move
{
	using copy_and_move::copy_and_move;
	using copy_and_move::operator=;

	move_only (move_only &&) = default;
	move_only &operator= (move_only &&) = default;

	move_only (const move_only &) = delete;
	move_only &operator= (const move_only &) = delete;
};


struct no_copy_or_move: public copy_and_move
{
	using copy_and_move::copy_and_move;
	using copy_and_move::operator=;

	no_copy_or_move (const no_copy_or_move &) = delete;
	no_copy_or_move &operator= (const no_copy_or_move &) = delete;

	no_copy_or_move (no_copy_or_move &&) = delete;
	no_copy_or_move &operator= (no_copy_or_move &&) = delete;
};


TEMPLATE_TEST_CASE("expected", ""
	, no_copy_or_move
	, copy_and_move
	, copy_only
	, move_only
	, void)
{
	using test_t = pal::expected<TestType, bool>;
	using other_t = pal::expected<int, char>;

	SECTION("expected()")
	{
		test_t x;
		CHECK(x.has_value());
	}

	SECTION("expected(in_place)")
	{
		test_t x{std::in_place};
		CHECK(x.has_value());
	}

	SECTION("expected(unexpect)")
	{
		test_t x{pal::unexpect, false};
		CHECK_FALSE(x.has_value());
	}

	SECTION("expected<T,E>(const expected<U,G> &)")
	{
		if constexpr (std::is_copy_constructible_v<TestType>)
		{
			SECTION("expected")
			{
				other_t y{2};
				REQUIRE(y.has_value());
				test_t x{y};
				REQUIRE(x.has_value());
				CHECK(x->base == *y);
			}

			SECTION("unexpected")
			{
				other_t y{pal::unexpect, '\1'};
				REQUIRE_FALSE(y.has_value());
				test_t x{y};
				REQUIRE_FALSE(x.has_value());
				CHECK(x.error() == true);
			}
		}
	}

	SECTION("expected<T,E>(expected<U,G> &&)")
	{
		if constexpr (std::is_move_constructible_v<TestType>)
		{
			SECTION("expected")
			{
				other_t y{2};
				REQUIRE(y.has_value());
				test_t x{std::move(y)};
				REQUIRE(x.has_value());
				CHECK(x->base == *y);
			}

			SECTION("unexpected")
			{
				other_t y{pal::unexpect, '\1'};
				REQUIRE_FALSE(y.has_value());
				test_t x{std::move(y)};
				REQUIRE_FALSE(x.has_value());
				CHECK(x.error() == true);
			}
		}
	}

	SECTION("expected(const unexpected<T> &)")
	{
		const pal::unexpected e{true};
		test_t x{e};
		REQUIRE_FALSE(x.has_value());
		CHECK(x.error());
	}

	SECTION("expected(unexpected<T> &&)")
	{
		pal::unexpected e{true};
		test_t x{std::move(e)};
		REQUIRE_FALSE(x.has_value());
		CHECK(x.error());
	}

	SECTION("expected(const unexpected<G> &)")
	{
		const pal::unexpected<char> e{'\1'};
		test_t x{e};
		REQUIRE_FALSE(x.has_value());
		CHECK(x.error());
	}

	SECTION("expected(unexpected<G> &&)")
	{
		pal::unexpected<char> e{'\1'};
		test_t x{std::move(e)};
		REQUIRE_FALSE(x.has_value());
		CHECK(x.error());
	}

	SECTION("assign")
	{
		if constexpr (std::is_copy_assignable_v<TestType> || std::is_void_v<TestType>)
		{
			SECTION("const expected &")
			{
				SECTION("x && y")
				{
					test_t x, y;
					x = y;
					CHECK(x.has_value());
				}

				SECTION("!x && y")
				{
					test_t x{pal::unexpect, true}, y;
					x = y;
					CHECK(x.has_value());
				}

				SECTION("x && !y")
				{
					test_t x, y{pal::unexpect, true};
					x = y;
					CHECK_FALSE(x.has_value());
				}

				SECTION("!x && !y")
				{
					test_t x{pal::unexpect, false}, y{pal::unexpect, true};
					x = y;
					CHECK_FALSE(x.has_value());
				}
			}
		}

		if constexpr (std::is_move_assignable_v<TestType> || std::is_void_v<TestType>)
		{
			SECTION("expected &&")
			{
				SECTION("x && y")
				{
					test_t x, y;
					x = std::move(y);
					CHECK(x.has_value());
				}

				SECTION("!x && y")
				{
					test_t x{pal::unexpect, true}, y;
					x = std::move(y);
					CHECK(x.has_value());
				}

				SECTION("x && !y")
				{
					test_t x, y{pal::unexpect, true};
					x = std::move(y);
					CHECK_FALSE(x.has_value());
				}

				SECTION("!x && !y")
				{
					test_t x{pal::unexpect, false}, y{pal::unexpect, true};
					x = std::move(y);
					CHECK_FALSE(x.has_value());
				}
			}
		}

		if constexpr (!std::is_void_v<TestType>)
		{
			SECTION("U &&")
			{
				SECTION("x")
				{
					test_t x;
					x = 2;
					CHECK(x.value().base == 2);
				}

				SECTION("!x")
				{
					test_t x{pal::unexpect, true};
					x = 2;
					CHECK(x.value().base == 2);
				}
			}
		}

		SECTION("const unexpected<G> &")
		{
			pal::unexpected e{false};

			SECTION("x")
			{
				test_t x;
				x = e;
				CHECK_FALSE(x.has_value());
			}

			SECTION("!x")
			{
				test_t x{pal::unexpect, true};
				x = e;
				CHECK_FALSE(x.has_value());
			}
		}

		SECTION("unexpected<G> &&")
		{
			pal::unexpected e{false};

			SECTION("x")
			{
				test_t x;
				x = std::move(e);
				CHECK_FALSE(x.has_value());
			}

			SECTION("!x")
			{
				test_t x{pal::unexpect, true};
				x = std::move(e);
				CHECK_FALSE(x.has_value());
			}
		}
	}

	SECTION("emplace")
	{
		if constexpr (std::is_void_v<TestType>)
		{
			SECTION("x")
			{
				test_t x;
				x.emplace();
				CHECK(x.has_value());
			}

			SECTION("!x")
			{
				test_t x{pal::unexpect, true};
				x.emplace();
				CHECK(x.has_value());
			}
		}
		else
		{
			SECTION("x")
			{
				test_t x{1};
				CHECK(x.emplace(2).base == 2);
			}

			SECTION("!x")
			{
				test_t x{pal::unexpect, true};
				x.emplace(2);
				CHECK(x.emplace(2).base == 2);
			}
		}
	}

	SECTION("swap")
	{
		if constexpr (std::is_move_constructible_v<TestType> || std::is_void_v<TestType>)
		{
			SECTION("x && y")
			{
				if constexpr (std::is_void_v<TestType>)
				{
					test_t x, y;
					x.swap(y);
					CHECK(x.has_value());
					CHECK(y.has_value());
				}
				else
				{
					test_t x{1}, y{2};
					x.swap(y);
					CHECK(x.value().base == 2);
					CHECK(y.value().base == 1);
				}
			}

			SECTION("!x && y")
			{
				test_t x{pal::unexpect, true}, y;
				x.swap(y);
				CHECK(x.has_value());
				CHECK(y.error() == true);
			}

			SECTION("x && !y")
			{
				test_t x, y{pal::unexpect, true};
				x.swap(y);
				CHECK(x.error() == true);
				CHECK(y.has_value());
			}

			SECTION("!x && !y")
			{
				test_t x{pal::unexpect, false}, y{pal::unexpect, true};
				x.swap(y);
				CHECK(x.error() == true);
				CHECK(y.error() == false);
			}
		}
	}

	SECTION("expected")
	{
		test_t x;
		REQUIRE(x.has_value());

		SECTION("expected(const expected &)")
		{
			if constexpr (std::is_copy_constructible_v<TestType>)
			{
				auto z{x};
				CHECK(z.has_value());
			}
		}

		SECTION("expected(expected &&)")
		{
			if constexpr (std::is_move_constructible_v<TestType>)
			{
				auto z{std::move(x)};
				CHECK(z.has_value());
			}
		}

		if constexpr (!std::is_void_v<TestType>)
		{
			SECTION("const T *operator-> () const")
			{
				CHECK(static_cast<const test_t &>(x)->fn() == 1);
			}

			SECTION("T *operator-> ()")
			{
				CHECK(x->fn() == 2);
			}

			SECTION("const T &operator* () const &")
			{
				CHECK((*static_cast<const test_t &>(x)).fn() == 1);
			}

			SECTION("T &operator* () &")
			{
				CHECK((*x).fn() == 2);
			}

			SECTION("const T &&operator* () const &&")
			{
				CHECK((*static_cast<const test_t &&>(std::move(x))).fn() == 3);
			}

			SECTION("T &&operator* () &&")
			{
				CHECK((*std::move(x)).fn() == 4);
			}

			SECTION("explicit operator bool")
			{
				CHECK(x);
			}

			SECTION("const T &value () const &")
			{
				CHECK(static_cast<const test_t &>(x).value().fn() == 1);
			}

			SECTION("T &value () &")
			{
				CHECK(x.value().fn() == 2);
			}

			SECTION("const T &&value () const &&")
			{
				CHECK(static_cast<const test_t &&>(std::move(x)).value().fn() == 3);
			}

			SECTION("T &&value () &&")
			{
				CHECK(std::move(x).value().fn() == 4);
			}

			SECTION("T value_or (U &&) const &")
			{
				if constexpr (std::is_copy_constructible_v<TestType>)
				{
					CHECK(x.value_or(0).fn() == 4);
				}
			}

			SECTION("T value_or (U &&v) &&")
			{
				if constexpr (std::is_move_constructible_v<TestType>)
				{
					CHECK(std::move(x).value_or(0).fn() == 4);
				}
			}
		}

		if constexpr (!pal::assert_noexcept)
		{
			SECTION("const E &error () const &")
			{
				CHECK_THROWS_AS(
					static_cast<const test_t &>(x).error(),
					std::logic_error
				);
			}

			SECTION("E &error () &")
			{
				CHECK_THROWS_AS(
					x.error(),
					std::logic_error
				);
			}

			SECTION("const E &&error () const &&")
			{
				CHECK_THROWS_AS(
					static_cast<const test_t &&>(std::move(x)).error(),
					std::logic_error
				);
			}

			SECTION("E &&error () &&")
			{
				CHECK_THROWS_AS(
					std::move(x).error(),
					std::logic_error
				);
			}
		}
	}

	SECTION("unexpected")
	{
		test_t x{pal::unexpect, true};
		REQUIRE_FALSE(x.has_value());

		SECTION("expected(const expected &)")
		{
			if constexpr (std::is_copy_constructible_v<TestType>)
			{
				auto y{x};
				CHECK_FALSE(y.has_value());
			}
		}

		SECTION("expected(expected &&)")
		{
			if constexpr (std::is_move_constructible_v<TestType>)
			{
				auto y{std::move(x)};
				CHECK_FALSE(y.has_value());
			}
		}

		SECTION("explicit operator bool")
		{
			CHECK(!x);
		}

		if constexpr (!std::is_void_v<TestType> && !pal::assert_noexcept)
		{
			SECTION("const T *operator-> () const")
			{
				const auto &y = x;
				CHECK_THROWS_AS(
					y->fn(),
					std::logic_error
				);
			}

			SECTION("T *operator-> ()")
			{
				CHECK_THROWS_AS(
					x->fn(),
					std::logic_error
				);
			}

			SECTION("const T &operator* () const &")
			{
				CHECK_THROWS_AS(
					(*static_cast<const test_t &>(x)).fn(),
					std::logic_error
				);
			}

			SECTION("T &operator* () &")
			{
				CHECK_THROWS_AS(
					(*x).fn(),
					std::logic_error
				);
			}

			SECTION("const T &&operator* () const &&")
			{
				CHECK_THROWS_AS(
					(*static_cast<const test_t &&>(std::move(x))).fn(),
					std::logic_error
				);
			}

			SECTION("T &&operator* () &&")
			{
				CHECK_THROWS_AS(
					(*std::move(x)).fn(),
					std::logic_error
				);
			}
		}

		if constexpr (!std::is_void_v<TestType>)
		{
			SECTION("const T &value () const &")
			{
				try
				{
					static_cast<const test_t &>(x).value().fn();
					FAIL();
				}
				catch (const pal::bad_expected_access<bool> &e)
				{
					CHECK(e.what() != nullptr);
					CHECK(e.error() == true);
				}
			}

			SECTION("T &value () &")
			{
				try
				{
					x.value().fn();
					FAIL();
				}
				catch (pal::bad_expected_access<bool> &e)
				{
					CHECK(e.what() != nullptr);
					CHECK(e.error() == true);
				}
			}

			SECTION("const T &&value () const &&")
			{
				try
				{
					static_cast<const test_t &&>(std::move(x)).value().fn();
					FAIL();
				}
				catch (const pal::bad_expected_access<bool> &e)
				{
					CHECK(e.what() != nullptr);
					CHECK(std::move(e).error() == true);
				}
			}

			SECTION("T &&value () &&")
			{
				try
				{
					std::move(x).value().fn();
					FAIL();
				}
				catch (pal::bad_expected_access<bool> &e)
				{
					CHECK(e.what() != nullptr);
					CHECK(std::move(e).error() == true);
				}
			}
		}

		SECTION("T value_or (U &&) const &")
		{
			if constexpr (std::is_copy_constructible_v<TestType>)
			{
				CHECK(x.value_or(0).fn() == 0);
			}
		}

		SECTION("T value_or (U &&v) &&")
		{
			if constexpr (std::is_move_constructible_v<TestType>)
			{
				CHECK(std::move(x).value_or(0).fn() == 0);
			}
		}

		SECTION("const E &error () const &")
		{
			CHECK(static_cast<const test_t &>(x).error() == true);
		}

		SECTION("E &error () &")
		{
			CHECK(x.error() == true);
		}

		SECTION("const E &&error () const &&")
		{
			CHECK(static_cast<const test_t &&>(std::move(x)).error() == true);
		}

		SECTION("E &&error () &&")
		{
			CHECK(std::move(x).error() == true);
		}
	}
}


template <typename TestType>
TestType value_1 ()
{
	return TestType{1};
}


template <typename TestType>
TestType value_2 ()
{
	return TestType{2};
}


template <>
std::string value_1 ()
{
	return "1";
}


template <>
std::string value_2 ()
{
	return "2";
}


TEMPLATE_TEST_CASE("expected::operator==", "", int, std::string)
{
	auto v1 = value_1<TestType>();
	auto v2 = value_2<TestType>();
	pal::unexpected<bool> u1{false}, u2{true};
	pal::expected<TestType, bool> ev1{v1}, ev2{v2}, eu1{u1}, eu2{u2};

	SECTION("x && y")
	{
		SECTION("expected & expected")
		{
			CHECK(ev1 == ev1);
			CHECK_FALSE(ev1 == ev2);
			CHECK(ev1 != ev2);
			CHECK_FALSE(ev1 != ev1);
		}

		SECTION("expected & T")
		{
			CHECK(ev1 == v1);
			CHECK(v1 == ev1);

			CHECK_FALSE(ev1 == v2);
			CHECK_FALSE(v2 == ev1);

			CHECK(ev1 != v2);
			CHECK(v2 != ev1);

			CHECK_FALSE(ev1 != v1);
			CHECK_FALSE(v1 != ev1);
		}

		SECTION("expected & unexpected")
		{
			CHECK_FALSE(ev1 == u1);
			CHECK_FALSE(u1 == ev1);

			CHECK(ev1 != u1);
			CHECK(u1 != ev1);
		}
	}

	SECTION("!x && y")
	{
		SECTION("expected & expected")
		{
			CHECK_FALSE(eu1 == ev1);
			CHECK(eu1 != ev1);
		}

		SECTION("expected & T")
		{
			CHECK_FALSE(eu1 == v1);
			CHECK_FALSE(v1 == eu1);

			CHECK(eu1 != v1);
			CHECK(v1 != eu1);
		}

		SECTION("expected & unexpected")
		{
			CHECK(eu1 == u1);
			CHECK(u1 == eu1);

			CHECK_FALSE(eu1 == u2);
			CHECK_FALSE(u2 == eu1);

			CHECK(eu1 != u2);
			CHECK(u2 != eu1);

			CHECK_FALSE(eu1 != u1);
			CHECK_FALSE(u1 != eu1);
		}
	}

	SECTION("x && !y")
	{
		SECTION("expected & expected")
		{
			CHECK_FALSE(ev1 == eu1);
			CHECK(ev1 != eu1);
		}
	}

	SECTION("!x && !y")
	{
		SECTION("expected & expected")
		{
			CHECK(eu1 == eu1);
			CHECK(eu1 != eu2);
		}
	}
}


TEMPLATE_TEST_CASE("expected::operator==", "", void)
{
	pal::unexpected<bool> u1{false}, u2{true};
	pal::expected<TestType, bool> ev1, ev2, eu1{u1}, eu2{u2};

	SECTION("x && y")
	{
		SECTION("expected & expected")
		{
			CHECK(ev1 == ev1);
			CHECK(ev1 == ev2);
			CHECK_FALSE(ev1 != ev2);
			CHECK_FALSE(ev1 != ev1);
		}

		SECTION("expected & unexpected")
		{
			CHECK_FALSE(ev1 == u1);
			CHECK_FALSE(u1 == ev1);

			CHECK(ev1 != u1);
			CHECK(u1 != ev1);
		}
	}

	SECTION("!x && y")
	{
		SECTION("expected & expected")
		{
			CHECK_FALSE(eu1 == ev1);
			CHECK(eu1 != ev1);
		}

		SECTION("expected & unexpected")
		{
			CHECK(eu1 == u1);
			CHECK(u1 == eu1);

			CHECK_FALSE(eu1 == u2);
			CHECK_FALSE(u2 == eu1);

			CHECK(eu1 != u2);
			CHECK(u2 != eu1);

			CHECK_FALSE(eu1 != u1);
			CHECK_FALSE(u1 != eu1);
		}
	}

	SECTION("x && !y")
	{
		SECTION("expected & expected")
		{
			CHECK_FALSE(ev1 == eu1);
			CHECK(ev1 != eu1);
		}
	}

	SECTION("!x && !y")
	{
		SECTION("expected & expected")
		{
			CHECK(eu1 == eu1);
			CHECK(eu1 != eu2);
		}
	}
}


constexpr uint16_t
	nothrow				= 0b00'00'00'00'00'00'00'00,
	//
	default_ctor_can_throw		= 0b00'00'00'00'00'00'00'01,
	default_ctor_throws		= 0b00'00'00'00'00'00'00'11,
	//
	emplace_ctor_can_throw		= 0b00'00'00'00'00'00'01'00,
	emplace_ctor_throws		= 0b00'00'00'00'00'00'11'00,
	//
	copy_ctor_can_throw		= 0b00'00'00'00'00'01'00'00,
	copy_ctor_throws		= 0b00'00'00'00'00'11'00'00,
	//
	move_ctor_can_throw		= 0b00'00'00'00'01'00'00'00,
	move_ctor_throws		= 0b00'00'00'00'11'00'00'00,
	//
	emplace_assign_can_throw	= 0b00'00'00'01'00'00'00'00,
	emplace_assign_throws		= 0b00'00'00'11'00'00'00'00,
	//
	copy_assign_can_throw		= 0b00'00'01'00'00'00'00'00,
	copy_assign_throws		= 0b00'00'11'00'00'00'00'00,
	//
	move_assign_can_throw		= 0b00'01'00'00'00'00'00'00,
	move_assign_throws		= 0b00'11'00'00'00'00'00'00,
	//
	ctor_can_throw			= default_ctor_can_throw | emplace_ctor_can_throw | copy_ctor_can_throw | move_ctor_can_throw,
	ctor_throws			= copy_ctor_throws | move_ctor_throws;


template <uint16_t ThrowControl>
constexpr bool no_throw (uint16_t flag)
{
	return (ThrowControl & flag) == 0;
}


template <uint16_t ThrowControl>
struct throwable
{
	bool value;

	throwable () noexcept (no_throw<ThrowControl>(default_ctor_can_throw))
		: value{false}
	{
		do_throw(ThrowControl, default_ctor_throws);
	}

	throwable (bool value) noexcept (no_throw<ThrowControl>(emplace_ctor_can_throw))
		: value{value}
	{
		do_throw(ThrowControl, emplace_ctor_throws);
	}

	throwable (const throwable &that) noexcept(no_throw<ThrowControl>(copy_ctor_can_throw))
		: value{that.value}
	{
		do_throw(ThrowControl, copy_ctor_throws);
	}

	throwable (throwable &&that) noexcept(no_throw<ThrowControl>(move_ctor_can_throw))
		: value{that.value}
	{
		do_throw(ThrowControl, move_ctor_throws);
	}

	throwable &operator= (const throwable &that) noexcept(no_throw<ThrowControl>(copy_assign_can_throw))
	{
		value = that.value;
		return do_throw(ThrowControl, copy_assign_throws);
	}

	throwable &operator= (throwable &&that) noexcept(no_throw<ThrowControl>(move_assign_can_throw))
	{
		value = std::move(that.value);
		return do_throw(ThrowControl, move_assign_throws);
	}

	throwable &operator= (bool value) noexcept(no_throw<ThrowControl>(emplace_assign_can_throw))
	{
		this->value = value;
		return do_throw(ThrowControl, emplace_assign_throws);
	}

private:

	throwable &do_throw (uint16_t control, uint16_t flag)
	{
		if ((control & flag) == flag)
		{
			throw true;
		}
		return *this;
	}
};


using value_ctor_nothrow = pal::expected<throwable<nothrow>, bool>;
using value_copy_ctor_can_throw = pal::expected<throwable<copy_ctor_can_throw>, bool>;
using value_move_ctor_can_throw = pal::expected<throwable<move_ctor_can_throw>, bool>;
using value_ctor_can_throw = pal::expected<throwable<ctor_can_throw>, bool>;
using error_copy_ctor_can_throw = pal::expected<bool, throwable<copy_ctor_can_throw>>;
using error_move_ctor_can_throw = pal::expected<bool, throwable<move_ctor_can_throw>>;
using error_ctor_can_throw = pal::expected<bool, throwable<ctor_can_throw>>;


TEMPLATE_TEST_CASE("expected::assign", "",
	value_ctor_nothrow,
	value_copy_ctor_can_throw,
	value_move_ctor_can_throw,
	value_ctor_can_throw,
	error_copy_ctor_can_throw,
	error_move_ctor_can_throw,
	error_ctor_can_throw)
{
	using T = TestType;

	auto [x, y] = GENERATE(table<T, T>(
	{
		{ T{false},                T{false}                },
		{ T{pal::unexpect, false}, T{false}                },
		{ T{false},                T{pal::unexpect, false} },
		{ T{pal::unexpect, false}, T{pal::unexpect, false} },
	}));

	SECTION("copy")
	{
		x = y;
	}

	SECTION("move")
	{
		x = std::move(y);
	}

	CHECK(x.has_value() == y.has_value());
}


using value_copy_ctor_throws = pal::expected<throwable<copy_ctor_throws>, bool>;
using value_ctor_throws = pal::expected<throwable<ctor_throws>, bool>;

TEMPLATE_TEST_CASE("expected::assign", "",
	value_copy_ctor_throws,
	value_ctor_throws)
{
	SECTION("!x && y")
	{
		TestType x{pal::unexpect, false}, y{true};

		SECTION("copy")
		{
			CHECK_THROWS_AS(x = y, bool);
		}

		SECTION("move")
		{
			if constexpr (std::is_same_v<TestType, value_ctor_throws>)
			{
				CHECK_THROWS_AS(x = std::move(y), bool);
			}
		}

		CHECK_FALSE(x.has_value());
		CHECK(y.has_value());
	}
}


using error_copy_ctor_throws = pal::expected<bool, throwable<copy_ctor_throws>>;
using error_ctor_throws = pal::expected<bool, throwable<ctor_throws>>;

TEMPLATE_TEST_CASE("expected::assign", "",
	error_copy_ctor_throws,
	error_ctor_throws)
{
	SECTION("x && !y")
	{
		TestType x{true}, y{pal::unexpect, false};

		SECTION("copy")
		{
			CHECK_THROWS_AS(x = y, bool);
		}

		SECTION("move")
		{
			if constexpr (std::is_same_v<TestType, error_ctor_throws>)
			{
				CHECK_THROWS_AS(x = std::move(y), bool);
			}
		}

		CHECK(x.has_value());
		CHECK_FALSE(y.has_value());
	}
}


struct value_from_u_throws
{
	value_from_u_throws (bool do_throw) noexcept(false)
	{
		if (do_throw)
		{
			throw true;
		}
	}

	value_from_u_throws &operator= (bool) noexcept(false)
	{
		throw true;
	}
};


TEMPLATE_TEST_CASE("expected::assign", "",
	value_from_u_throws)
{
	using T = pal::expected<TestType, bool>;

	SECTION("x")
	{
		T x{false};
		CHECK_THROWS_AS(x = true, bool);
		CHECK(x.has_value());
	}

	SECTION("!x throws")
	{
		T x{pal::unexpect, false};
		CHECK_THROWS_AS(x = true, bool);
		CHECK_FALSE(x.has_value());
	}

	SECTION("!x does not throw")
	{
		T x{pal::unexpect, false};
		CHECK_NOTHROW(x = false);
		CHECK(x.has_value());
	}
}


using value_emplace_ctor_can_throw = pal::expected<throwable<emplace_ctor_can_throw>, bool>;

TEMPLATE_TEST_CASE("expected::emplace", "",
	value_ctor_nothrow,
	value_emplace_ctor_can_throw,
	value_ctor_can_throw)
{
	SECTION("x")
	{
		TestType x{true};
		CHECK(x.emplace(false).value == false);
	}

	SECTION("!x")
	{
		TestType x{pal::unexpect, false};
		CHECK(x.emplace(false).value == false);
	}
}


using value_emplace_ctor_throws_1 = pal::expected<throwable<ctor_can_throw | emplace_ctor_throws>, bool>;
using value_emplace_ctor_throws_2 = pal::expected<throwable<emplace_ctor_throws>, bool>;

TEMPLATE_TEST_CASE("expected::emplace", "",
	value_emplace_ctor_throws_1,
	value_emplace_ctor_throws_2)
{
	SECTION("!x")
	{
		TestType x{pal::unexpect, false};
		CHECK_THROWS_AS(x.emplace(false), bool);
		CHECK_FALSE(x.has_value());
	}
}


using move_ctor_can_throw_t = throwable<move_ctor_can_throw>;
inline void swap (move_ctor_can_throw_t &l, move_ctor_can_throw_t &r) noexcept
{
	using std::swap;
	swap(l.value, r.value);
}


using move_ctor_throws_t = throwable<move_ctor_throws>;
inline void swap (move_ctor_throws_t &l, move_ctor_throws_t &r) noexcept
{
	using std::swap;
	swap(l.value, r.value);
}


TEST_CASE("expected::swap")
{
	using std::swap;

	SECTION("nothrow_move_constructible<T>")
	{
		using ThrowableType = pal::expected<move_ctor_can_throw_t, bool>;
		using ThrowType = pal::expected<move_ctor_throws_t, bool>;

		SECTION("x && y")
		{
			ThrowableType x{true}, y{false};
			CHECK(x->value);
			CHECK_FALSE(y->value);
			swap(x, y);
			CHECK_FALSE(x->value);
			CHECK(y->value);
		}

		SECTION("!x && y")
		{
			ThrowableType x{pal::unexpect, false}, y{false};
			CHECK_FALSE(x.has_value());
			CHECK_FALSE(y.value().value);
			swap(x, y);
			CHECK_FALSE(x.value().value);
			CHECK_FALSE(y.has_value());
		}

		SECTION("x && !y")
		{
			ThrowableType x{false}, y{pal::unexpect, false};
			CHECK_FALSE(x.value().value);
			CHECK_FALSE(y.has_value());
			swap(x, y);
			CHECK_FALSE(x.has_value());
			CHECK_FALSE(y.value().value);
		}

		SECTION("!x && !y")
		{
			ThrowableType x{pal::unexpect, false}, y{pal::unexpect, false};
			CHECK_FALSE(x.has_value());
			CHECK_FALSE(y.has_value());
			swap(x, y);
			CHECK_FALSE(x.has_value());
			CHECK_FALSE(y.has_value());
		}

		SECTION("throw")
		{
			ThrowType x{true}, y{pal::unexpect, false};
			CHECK(x->value);
			CHECK_FALSE(y.has_value());
			CHECK_THROWS_AS(swap(x, y), bool);
			CHECK(x->value);
			CHECK_FALSE(y.has_value());
		}
	}

	SECTION("nothrow_move_constructible<E>")
	{
		using ThrowableType = pal::expected<bool, move_ctor_can_throw_t>;
		using ThrowType = pal::expected<bool, move_ctor_throws_t>;

		SECTION("x && y")
		{
			ThrowableType x{true}, y{false};
			CHECK(*x);
			CHECK_FALSE(*y);
			swap(x, y);
			CHECK_FALSE(*x);
			CHECK(*y);
		}

		SECTION("!x && y")
		{
			ThrowableType x{pal::unexpect, false}, y{true};
			CHECK_FALSE(x.has_value());
			CHECK(*y);
			swap(x, y);
			CHECK(*x);
			CHECK_FALSE(y.has_value());;
		}

		SECTION("x && !y")
		{
			ThrowableType x{true}, y{pal::unexpect, false};
			CHECK(*x);
			CHECK_FALSE(y.has_value());
			swap(x, y);
			CHECK_FALSE(x.has_value());
			CHECK(*y);
		}

		SECTION("!x && !y")
		{
			ThrowableType x{pal::unexpect, false}, y{pal::unexpect, false};
			CHECK_FALSE(x.has_value());
			CHECK_FALSE(y.has_value());
			swap(x, y);
			CHECK_FALSE(x.has_value());
			CHECK_FALSE(y.has_value());
		}

		SECTION("throw")
		{
			ThrowType x{pal::unexpect, true}, y{true};
			CHECK(x.error().value);
			CHECK(*y);
			CHECK_THROWS_AS(swap(x, y), bool);
			CHECK(x.error().value);
			CHECK(*y);
		}
	}
}


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


#if defined(_MSC_BUILD) && !defined(_DEBUG)
	#pragma warning(pop)
#endif
