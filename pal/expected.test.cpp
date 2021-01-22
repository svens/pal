#if defined(_MSC_BUILD) && !defined(_DEBUG)
	// With throwable<> some intentional test-throws make parts of code
	// unreachable for MSVC Release build.
	// As it is test-specific, no point to try to find workaround.
	#pragma warning(push)
	#pragma warning(disable: 4702)
#endif

#include <pal/expected>
#include <pal/test>


namespace {

using namespace pal_test;

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
		pal::unexpected e{std::in_place, v1};
		CHECK(e.value() == v1);
	}

	SECTION("unexpected(U&&)")
	{
		pal::unexpected e{v1};
		CHECK(e.value() == v1);
	}

	SECTION("unexpected(const unexpected<E> &)")
	{
		pal::unexpected that{v1};
		pal::unexpected e{that};
		CHECK(e.value() == v1);
	}

	SECTION("unexpected(unexpected<E> &&)")
	{
		pal::unexpected that{v1};
		pal::unexpected e{std::move(that)};
		CHECK(e.value() == v1);
	}

	SECTION("unexpected(const unexpected<U> &)")
	{
		pal::unexpected that{v2};
		pal::unexpected<TestType> e{that};
		CHECK(e.value() == v2);
		CHECK(!std::is_same_v<decltype(e), decltype(that)>);
	}

	SECTION("unexpected(unexpected<U> &&)")
	{
		pal::unexpected that{v2};
		pal::unexpected<TestType> e{std::move(that)};
		CHECK(e.value() == v2);
		CHECK(!std::is_same_v<decltype(e), decltype(that)>);
	}

	SECTION("operator=(const unexpected<U> &)")
	{
		pal::unexpected that{v2};
		pal::unexpected e{v1};
		e = that;
		CHECK(e.value() == v2);
		CHECK(!std::is_same_v<decltype(e), decltype(that)>);
	}

	SECTION("operator=(unexpected<U> &&)")
	{
		pal::unexpected that{v2};
		pal::unexpected e{v1};
		e = std::move(that);
		CHECK(e.value() == v2);
		CHECK(!std::is_same_v<decltype(e), decltype(that)>);
	}

	SECTION("const value &")
	{
		pal::unexpected e{v1};
		CHECK(static_cast<const decltype(e) &>(e).value() == v1);
	}

	SECTION("value &")
	{
		pal::unexpected e{v1};
		CHECK(static_cast<decltype(e) &>(e).value() == v1);
	}

	SECTION("const value &&")
	{
		pal::unexpected e{v1};
		CHECK(std::forward<const decltype(e) &&>(e).value() == v1);
	}

	SECTION("value &&")
	{
		pal::unexpected e{v1};
		CHECK(std::forward<decltype(e) &&>(e).value() == v1);
	}

	SECTION("swap")
	{
		pal::unexpected e1{v1}, e2{static_cast<decltype(v1)>(v2)};
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
	static_assert(x.value() == value);

	constexpr pal::unexpected y{std::in_place, value};
	static_assert(y.value() == value);

	constexpr pal::unexpected w{x};
	static_assert(w.value() == value);

	constexpr pal::unexpected z{std::move(y)};
	static_assert(z.value() == value);
}


// expected<T, E> {{{1

struct trivial //{{{2
{
	int base{1};

	trivial () = default;
	trivial (const trivial &) = default;
	trivial (trivial &&) = default;
	trivial &operator= (const trivial &) = default;
	trivial &operator= (trivial &&) = default;
	~trivial () = default;

	constexpr trivial (int v) noexcept
		: base{v}
	{}

	trivial &operator= (int v) noexcept
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
static_assert(std::is_trivially_copy_constructible_v<trivial>);
static_assert(std::is_trivially_move_constructible_v<trivial>);
static_assert(std::is_trivially_destructible_v<trivial>);


struct non_trivial: trivial //{{{2
{
	using trivial::trivial;
	using trivial::operator=;
	~non_trivial () { }
};
static_assert(std::is_trivially_copy_constructible_v<non_trivial> == false);
static_assert(std::is_trivially_move_constructible_v<non_trivial> == false);
static_assert(std::is_trivially_destructible_v<non_trivial> == false);


template <typename Base>
struct copy_and_move: Base //{{{2
{
	using Base::Base;
	using Base::operator=;
};


template <typename Base>
struct copy_only: Base //{{{2
{
	using Base::Base;
	using Base::operator=;

	copy_only (copy_only &&) = delete;
	copy_only &operator= (copy_only &&) = delete;
};


template <typename Base>
struct move_only: Base //{{{2
{
	using Base::Base;
	using Base::operator=;

	move_only (const move_only &) = delete;
	move_only &operator= (const move_only &) = delete;
};


template <typename Base>
struct no_copy_or_move: Base //{{{2
{
	using Base::Base;
	using Base::operator=;

	no_copy_or_move (const no_copy_or_move &) = delete;
	no_copy_or_move &operator= (const no_copy_or_move &) = delete;

	no_copy_or_move (no_copy_or_move &&) = delete;
	no_copy_or_move &operator= (no_copy_or_move &&) = delete;
};


//}}}2


TEMPLATE_TEST_CASE("expected", "",
	no_copy_or_move<trivial>,
	copy_and_move<trivial>,
	copy_only<trivial>,
	move_only<trivial>,
	no_copy_or_move<non_trivial>,
	copy_and_move<non_trivial>,
	copy_only<non_trivial>,
	move_only<non_trivial>,
	void)
{
	using T = pal::expected<TestType, int>;
	using other_t = pal::expected<int, short>;

	//
	// Constructors
	//

	SECTION("expected()")
	{
		T e;
		if constexpr (!std::is_void_v<TestType>)
		{
			CHECK(e->base == 1);
		}
	}

	SECTION("expected(const expected &)")
	{
		if constexpr (std::is_copy_constructible_v<TestType>)
		{
			T e;
			auto e1{e};
			CHECK(e1.value().base == e.value().base);

			T u{pal::unexpect, 100};
			auto u1{u};
			CHECK(u1.error() == u.error());
		}
	}

	SECTION("expected(expected &&)")
	{
		if constexpr (std::is_move_constructible_v<TestType>)
		{
			T e;
			auto e1{std::move(e)};
			CHECK(e1.value().base == e.value().base);

			T u{pal::unexpect, 100};
			auto u1{std::move(u)};
			CHECK(u1.error() == u.error());
		}
	}

	SECTION("expected(const expected<U, G> &)")
	{
		if constexpr (std::is_copy_constructible_v<TestType>)
		{
			other_t e1{std::in_place, 1};
			T e{e1};
			CHECK(e->base == *e1);

			other_t u1{pal::unexpect, (short)100};
			T u{u1};
			CHECK(u.error() == u1.error());
		}
	}

	SECTION("expected(expected<U, G> &&)")
	{
		if constexpr (std::is_move_constructible_v<TestType>)
		{
			other_t e1{std::in_place, 1};
			T e{std::move(e1)};
			CHECK(e->base == *e1);

			other_t u1{pal::unexpect, (short)100};
			T u{std::move(u1)};
			CHECK(u.error() == u1.error());
		}
	}

	SECTION("expected(U &&)")
	{
		if constexpr (!std::is_void_v<TestType>)
		{
			T e{1};
			CHECK(e->base == 1);
		}
	}

	SECTION("expected(const unexpected<E> &)")
	{
		const pal::unexpected u{short(100)};
		T e{u};
		CHECK(!e);
		CHECK(e.error() == 100);
	}

	SECTION("expected(const unexpected<G> &)")
	{
		const pal::unexpected<char> u{'\1'};
		T e{u};
		CHECK(!e);
		CHECK(e.error() == 1);
	}

	SECTION("expected(unexpected<E> &&)")
	{
		pal::unexpected u{short(100)};
		T e{std::move(u)};
		CHECK(!e);
		CHECK(e.error() == 100);
	}

	SECTION("expected(unexpected<G> &&)")
	{
		pal::unexpected<char> u{'\1'};
		T e{std::move(u)};
		CHECK(!e);
		CHECK(e.error() == 1);
	}

	SECTION("expected(in_place)")
	{
		if constexpr (std::is_void_v<TestType>)
		{
			T e{std::in_place};
			CHECK(e);
		}
		else
		{
			T e{std::in_place, 2};
			CHECK(e);
			CHECK(e->base == 2);
		}
	}

	SECTION("expected(unexpect)")
	{
		T u{pal::unexpect, 100};
		CHECK(!u);
		CHECK(u.error() == 100);
	}

	//
	// Assignment
	//

	SECTION("operator=(const expected &)")
	{
		if constexpr (std::is_copy_assignable_v<TestType> || std::is_void_v<TestType>)
		{
			SECTION("x && y")
			{
				T x, y;
				x = y;
				CHECK(x);
			}

			SECTION("!x && y")
			{
				T x{pal::unexpect, 100}, y;
				x = y;
				CHECK(x);
			}

			SECTION("x && !y")
			{
				T x, y{pal::unexpect, 100};
				x = y;
				CHECK_FALSE(x);
			}

			SECTION("!x && !y")
			{
				T x{pal::unexpect, false}, y{pal::unexpect, 100};
				x = y;
				CHECK_FALSE(x);
			}
		}
	}

	SECTION("operator=(expected &&)")
	{
		if constexpr (std::is_move_assignable_v<TestType> || std::is_void_v<TestType>)
		{
			SECTION("x && y")
			{
				T x, y;
				x = std::move(y);
				CHECK(x.has_value());
			}

			SECTION("!x && y")
			{
				T x{pal::unexpect, 100}, y;
				x = std::move(y);
				CHECK(x.has_value());
			}

			SECTION("x && !y")
			{
				T x, y{pal::unexpect, 100};
				x = std::move(y);
				CHECK_FALSE(x.has_value());
			}

			SECTION("!x && !y")
			{
				T x{pal::unexpect, false}, y{pal::unexpect, 100};
				x = std::move(y);
				CHECK_FALSE(x.has_value());
			}
		}
	}

	SECTION("operator=(const unexpected<G> &)")
	{
		pal::unexpected e{100};

		SECTION("x")
		{
			T x;
			x = e;
			CHECK_FALSE(x.has_value());
		}

		SECTION("!x")
		{
			T x{pal::unexpect, 100};
			x = e;
			CHECK_FALSE(x.has_value());
		}
	}

	SECTION("operator=(unexpected<G> &&)")
	{
		pal::unexpected e{100};

		SECTION("x")
		{
			T x;
			x = std::move(e);
			CHECK_FALSE(x.has_value());
		}

		SECTION("!x")
		{
			T x{pal::unexpect, 100};
			x = std::move(e);
			CHECK_FALSE(x.has_value());
		}
	}

	SECTION("operator=(U &&)")
	{
		if constexpr (!std::is_void_v<TestType>)
		{
			SECTION("x")
			{
				T x;
				x = 2;
				CHECK(x.value().base == 2);
			}

			SECTION("!x")
			{
				T x{pal::unexpect, 100};
				x = 2;
				CHECK(x.value().base == 2);
			}
		}
	}

	SECTION("emplace")
	{
		if constexpr (std::is_void_v<TestType>)
		{
			SECTION("x")
			{
				T x;
				x.emplace();
				CHECK(x.has_value());
			}

			SECTION("!x")
			{
				T x{pal::unexpect, 100};
				x.emplace();
				CHECK(x.has_value());
			}
		}
		else
		{
			SECTION("x")
			{
				T x{1};
				CHECK(x.emplace(2).base == 2);
			}

			SECTION("!x")
			{
				T x{pal::unexpect, 100};
				x.emplace(2);
				CHECK(x.emplace(2).base == 2);
			}
		}
	}


	//
	// Swap
	//

	SECTION("swap")
	{
		if constexpr (std::is_move_constructible_v<TestType> || std::is_void_v<TestType>)
		{
			SECTION("x && y")
			{
				if constexpr (std::is_void_v<TestType>)
				{
					T x, y;
					x.swap(y);
					CHECK(x.has_value());
					CHECK(y.has_value());
				}
				else
				{
					T x{1}, y{2};
					x.swap(y);
					CHECK(x.value().base == 2);
					CHECK(y.value().base == 1);
				}
			}

			SECTION("!x && y")
			{
				T x{pal::unexpect, 100}, y;
				x.swap(y);
				CHECK(x.has_value());
				CHECK(y.error() == 100);
			}

			SECTION("x && !y")
			{
				T x, y{pal::unexpect, 100};
				x.swap(y);
				CHECK(x.error() == 100);
				CHECK(y.has_value());
			}

			SECTION("!x && !y")
			{
				T x{pal::unexpect, 100}, y{pal::unexpect, 200};
				x.swap(y);
				CHECK(x.error() == 200);
				CHECK(y.error() == 100);
			}
		}
	}


	//
	// Observers
	//

	SECTION("observers")
	{
		T e;
		CHECK(e);
		CHECK(e.has_value());

		T u{pal::unexpect, 100};
		CHECK(!u);
		CHECK_FALSE(u.has_value());

		if constexpr (!std::is_void_v<TestType>)
		{
			SECTION("const T *operator-> () const")
			{
				CHECK(static_cast<const T &>(e)->fn() == 1);
			}

			SECTION("T *operator-> ()")
			{
				CHECK(e->fn() == 2);
			}

			SECTION("const T &operator* () const &")
			{
				CHECK((*static_cast<const T &>(e)).fn() == 1);
			}

			SECTION("T &operator* () &")
			{
				CHECK((*e).fn() == 2);
			}

			SECTION("const T &&operator* () const &&")
			{
				CHECK((*static_cast<const T &&>(std::move(e))).fn() == 3);
			}

			SECTION("T &&operator* () &&")
			{
				CHECK((*std::move(e)).fn() == 4);
			}

			SECTION("const E &error () const &")
			{
				CHECK(static_cast<const T &>(e).error());
			}

			SECTION("E &error () &")
			{
				CHECK(e.error());
			}

			SECTION("const E &&error () const &&")
			{
				CHECK(static_cast<const T &&>(std::move(e)).error());
			}

			SECTION("E &&error () &&")
			{
				CHECK(std::move(e).error());
			}

			SECTION("const T &value () const &")
			{
				CHECK(static_cast<const T &>(e).value().fn() == 1);

				try
				{
					static_cast<const T &>(u).value();
					FAIL();
				}
				catch (const pal::bad_expected_access<int> &e)
				{
					CHECK(e.what() != nullptr);
					CHECK(e.error() == 100);
				}
			}

			SECTION("T &value () &")
			{
				CHECK(e.value().fn() == 2);

				try
				{
					u.value();
					FAIL();
				}
				catch (pal::bad_expected_access<int> &e)
				{
					CHECK(e.what() != nullptr);
					CHECK(e.error() == 100);
				}
			}

			SECTION("const T &&value () const &&")
			{
				CHECK(static_cast<const T &&>(std::move(e)).value().fn() == 3);

				try
				{
					static_cast<const T &&>(std::move(u)).value();
					FAIL();
				}
				catch (const pal::bad_expected_access<int> &e)
				{
					CHECK(e.what() != nullptr);
					CHECK(std::move(e).error() == 100);
				}
			}

			SECTION("T &&value () &&")
			{
				CHECK(std::move(e).value().fn() == 4);

				try
				{
					std::move(u).value();
					FAIL();
				}
				catch (pal::bad_expected_access<int> &e)
				{
					CHECK(e.what() != nullptr);
					CHECK(std::move(e).error() == 100);
				}
			}

			SECTION("T value_or (U &&) const &")
			{
				if constexpr (std::is_copy_constructible_v<TestType>)
				{
					CHECK(e.value_or(0).fn() == 4);
					CHECK(u.value_or(10).fn() == 40);
				}
			}

			SECTION("T value_or (U &&v) &&")
			{
				if constexpr (std::is_move_constructible_v<TestType>)
				{
					CHECK(std::move(e).value_or(0).fn() == 4);
					CHECK(std::move(u).value_or(10).fn() == 40);
				}
			}
		}
	}
}


// expected<T, E>::constexpr {{{1

TEMPLATE_TEST_CASE("expected::constexpr", "",
	no_copy_or_move<trivial>,
	copy_and_move<trivial>,
	copy_only<trivial>,
	move_only<trivial>,
	void)
{
	using T = pal::expected<TestType, int>;

	static_assert(pal::is_expected_v<T>);
	static_assert(!pal::is_expected_v<TestType>);

	constexpr T e;
	static_assert(e);

	if constexpr (!std::is_void_v<TestType>)
	{
		static_assert(e->base == 1);
		static_assert((*e).base == 1);
		static_assert(e.value().base == 1);

		constexpr T e1{2};
		static_assert(e1);
		static_assert(e1->base == 2);
	}

	constexpr T u{pal::unexpect, 100};
	static_assert(!u);
	static_assert(u.error() == 100);

	if constexpr (std::is_copy_constructible_v<TestType>)
	{
		static_assert(e.value_or(2).base == 1);
		static_assert(u.value_or(2).base == 2);

		constexpr T e1{e};
		static_assert(e1);
		static_assert(e1->base == e->base);

		constexpr T eu{u};
		static_assert(!eu);
		static_assert(eu.error() == u.error());
	}
}


// expected<T, E>::operator= {{{1

using value_ctor_nothrow = pal::expected<throwable<nothrow>, bool>;
using value_ctor_can_throw = pal::expected<throwable<ctor_can_throw>, bool>;
using value_copy_ctor_can_throw = pal::expected<throwable<copy_ctor_can_throw>, bool>;
using value_copy_ctor_throws = pal::expected<throwable<copy_ctor_throws>, bool>;
using value_move_ctor_can_throw = pal::expected<throwable<move_ctor_can_throw>, bool>;
using value_copy_and_move_ctor_throws = pal::expected<throwable<copy_and_move_ctor_throws>, bool>;
using value_emplace_ctor_can_throw = pal::expected<throwable<emplace_ctor_can_throw>, bool>;

using error_ctor_can_throw = pal::expected<bool, throwable<ctor_can_throw>>;
using error_copy_ctor_can_throw = pal::expected<bool, throwable<copy_ctor_can_throw>>;
using error_move_ctor_can_throw = pal::expected<bool, throwable<move_ctor_can_throw>>;
using error_copy_ctor_throws = pal::expected<bool, throwable<copy_ctor_throws>>;
using error_copy_and_move_ctor_throws = pal::expected<bool, throwable<copy_and_move_ctor_throws>>;

TEMPLATE_TEST_CASE("expected::operator=", "",
	value_ctor_nothrow,
	value_ctor_can_throw,
	value_copy_ctor_can_throw,
	value_move_ctor_can_throw,
	error_ctor_can_throw,
	error_copy_ctor_can_throw,
	error_move_ctor_can_throw)
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

TEMPLATE_TEST_CASE("expected::operator=", "",
	value_copy_ctor_throws,
	value_copy_and_move_ctor_throws)
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
			if constexpr (std::is_same_v<TestType, value_copy_and_move_ctor_throws>)
			{
				CHECK_THROWS_AS(x = std::move(y), bool);
			}
		}

		CHECK(!x);
		CHECK(y);
	}
}

TEMPLATE_TEST_CASE("expected::operator=", "",
	error_copy_ctor_throws,
	error_copy_and_move_ctor_throws)
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
			if constexpr (std::is_same_v<TestType, error_copy_and_move_ctor_throws>)
			{
				CHECK_THROWS_AS(x = std::move(y), bool);
			}
		}

		CHECK(x);
		CHECK(!y);
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

TEMPLATE_TEST_CASE("expected::operator=", "",
	value_from_u_throws)
{
	using T = pal::expected<TestType, bool>;

	SECTION("x")
	{
		T x{false};
		CHECK_THROWS_AS(x = true, bool);
		CHECK(x);
	}

	SECTION("!x throws")
	{
		T x{pal::unexpect, false};
		CHECK_THROWS_AS(x = true, bool);
		CHECK(!x);
	}

	SECTION("!x does not throw")
	{
		T x{pal::unexpect, false};
		CHECK_NOTHROW(x = false);
		CHECK(x);
	}
}


// expected<T, E>::emplace {{{1

TEMPLATE_TEST_CASE("expected::emplace", "",
	value_ctor_nothrow,
	value_emplace_ctor_can_throw,
	value_ctor_can_throw)
{
	SECTION("x")
	{
		TestType x{true};
		CHECK(x.emplace(false).value == false);
		CHECK(x);
	}

	SECTION("!x")
	{
		TestType x{pal::unexpect, false};
		CHECK(x.emplace(false).value == false);
		CHECK(x);
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


// expected<T, E>::swap {{{1

TEST_CASE("expected::swap")
{
	using std::swap;

	using move_ctor_can_throw_t = throwable<move_ctor_can_throw>;
	using move_ctor_throws_t = throwable<move_ctor_throws>;

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


// expected<T, E>::operator== {{{1

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

TEMPLATE_TEST_CASE("expected::operator==", "",
	int, std::string)
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

TEMPLATE_TEST_CASE("expected::operator==", "",
	void)
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


// expected<T, E>::and_then {{{1

TEST_CASE("expected::and_then")
{
	SECTION("int")
	{
		using T = pal::expected<int, int>;

		auto fe = [](int v) { return T{v * v}; };

		typename T::value_type fv_arg{};
		auto fv = [&](int v) { fv_arg = v; };

		SECTION("expected")
		{
			SECTION("const T &")
			{
				const T e = 2;
				CHECK(e.and_then(fe).value() == 4);
				CHECK(e.and_then(fv).value() == 2);
			}

			SECTION("T &")
			{
				T e = 2;
				CHECK(e.and_then(fe).value() == 4);
				CHECK(e.and_then(fv).value() == 2);
			}

			SECTION("const T &&")
			{
				const T e = 2;
				CHECK(std::move(e).and_then(fe).value() == 4);
				CHECK(std::move(e).and_then(fv).value() == 2);
			}

			SECTION("T &&")
			{
				T e = 2;
				CHECK(std::move(e).and_then(fe).value() == 4);
				CHECK(std::move(e).and_then(fv).value() == 2);
			}

			CHECK(fv_arg == 2);
		}

		SECTION("unexpected")
		{
			SECTION("const T &")
			{
				const T e{pal::unexpect, 2};
				CHECK(e.and_then(fe).error() == 2);
				CHECK(e.and_then(fv).error() == 2);
			}

			SECTION("T &")
			{
				T e{pal::unexpect, 2};
				CHECK(e.and_then(fe).error() == 2);
				CHECK(e.and_then(fv).error() == 2);
			}

			SECTION("const T &&")
			{
				const T e{pal::unexpect, 2};
				CHECK(std::move(e).and_then(fe).error() == 2);
				CHECK(std::move(e).and_then(fv).error() == 2);
			}

			SECTION("T &&")
			{
				T e{pal::unexpect, 2};
				CHECK(std::move(e).and_then(fe).error() == 2);
				CHECK(std::move(e).and_then(fv).error() == 2);
			}

			CHECK(fv_arg == 0);
		}
	}

	SECTION("void")
	{
		using T = pal::expected<void, int>;

		auto fe = []() { return T{}; };

		bool fv_called = false;
		auto fv = [&]() { fv_called = true; };

		SECTION("expected")
		{
			SECTION("const T &")
			{
				const T e;
				CHECK(e.and_then(fe).has_value());
				CHECK(e.and_then(fv).has_value());
			}

			SECTION("T &")
			{
				T e;
				CHECK(e.and_then(fe).has_value());
				CHECK(e.and_then(fv).has_value());
			}

			SECTION("const T &&")
			{
				const T e;
				CHECK(std::move(e).and_then(fe).has_value());
				CHECK(std::move(e).and_then(fv).has_value());
			}

			SECTION("T &&")
			{
				T e;
				CHECK(std::move(e).and_then(fe).has_value());
				CHECK(std::move(e).and_then(fv).has_value());
			}

			CHECK(fv_called);
		}

		SECTION("unexpected")
		{
			SECTION("const T &")
			{
				const T e{pal::unexpect, 2};
				CHECK(e.and_then(fe).error() == 2);
				CHECK(e.and_then(fv).error() == 2);
			}

			SECTION("T &")
			{
				T e{pal::unexpect, 2};
				CHECK(e.and_then(fe).error() == 2);
				CHECK(e.and_then(fv).error() == 2);
			}

			SECTION("const T &&")
			{
				const T e{pal::unexpect, 2};
				CHECK(std::move(e).and_then(fe).error() == 2);
				CHECK(std::move(e).and_then(fv).error() == 2);
			}

			SECTION("T &&")
			{
				T e{pal::unexpect, 2};
				CHECK(std::move(e).and_then(fe).error() == 2);
				CHECK(std::move(e).and_then(fv).error() == 2);
			}

			CHECK(fv_called == false);
		}
	}

	SECTION("chain")
	{
		using T = pal::expected<int, int>;

		auto ok = [&](int v) { return T{v * v}; };
		auto err = [&](int v) { return T{pal::unexpect, v}; };

		CHECK(T{2}.and_then(ok).and_then(ok).value() == 16);
		CHECK(T{2}.and_then(ok).and_then(err).error() == 4);
		CHECK(T{2}.and_then(err).and_then(err).error() == 2);
		CHECK(T{pal::unexpect, 3}.and_then(err).and_then(err).error() == 3);
	}
}


// expected<T, E>::or_else {{{1

TEST_CASE("expected::or_else")
{
	SECTION("int")
	{
		using T = pal::expected<int, int>;

		auto fe = [](int v) { return T{v * v}; };

		typename T::value_type fv_arg{};
		auto fv = [&](int v) { fv_arg = v; };

		SECTION("expected")
		{
			SECTION("const T &")
			{
				const T e = 2;
				CHECK(e.or_else(fe).value() == 2);
				CHECK(e.or_else(fv).value() == 2);
			}

			SECTION("T &")
			{
				T e = 2;
				CHECK(e.or_else(fe).value() == 2);
				CHECK(e.or_else(fv).value() == 2);
			}

			SECTION("const T &&")
			{
				const T e = 2;
				CHECK(std::move(e).or_else(fe).value() == 2);
				CHECK(std::move(e).or_else(fv).value() == 2);
			}

			SECTION("T &&")
			{
				T e = 2;
				CHECK(std::move(e).or_else(fe).value() == 2);
				CHECK(std::move(e).or_else(fv).value() == 2);
			}

			CHECK(fv_arg == 0);
		}

		SECTION("unexpected")
		{
			SECTION("const T &")
			{
				const T e{pal::unexpect, 2};
				CHECK(e.or_else(fe).value() == 4);
				CHECK(e.or_else(fv).error() == 2);
			}

			SECTION("T &")
			{
				T e{pal::unexpect, 2};
				CHECK(e.or_else(fe).value() == 4);
				CHECK(e.or_else(fv).error() == 2);
			}

			SECTION("const T &&")
			{
				const T e{pal::unexpect, 2};
				CHECK(std::move(e).or_else(fe).value() == 4);
				CHECK(std::move(e).or_else(fv).error() == 2);
			}

			SECTION("T &&")
			{
				T e{pal::unexpect, 2};
				CHECK(std::move(e).or_else(fe).value() == 4);
				CHECK(std::move(e).or_else(fv).error() == 2);
			}

			CHECK(fv_arg == 2);
		}
	}

	SECTION("void")
	{
		using T = pal::expected<void, int>;

		auto fe = [](int v) { return T{pal::unexpect, v * v}; };

		typename T::error_type fv_arg{};
		auto fv = [&](int v) { fv_arg = v; };

		SECTION("expected")
		{
			SECTION("const T &")
			{
				const T e;
				CHECK(e.or_else(fe).has_value());
				CHECK(e.or_else(fv).has_value());
			}

			SECTION("T &")
			{
				T e;
				CHECK(e.or_else(fe).has_value());
				CHECK(e.or_else(fv).has_value());
			}

			SECTION("const T &&")
			{
				const T e;
				CHECK(std::move(e).or_else(fe).has_value());
				CHECK(std::move(e).or_else(fv).has_value());
			}

			SECTION("T &&")
			{
				T e;
				CHECK(std::move(e).or_else(fe).has_value());
				CHECK(std::move(e).or_else(fv).has_value());
			}

			CHECK(fv_arg == 0);
		}

		SECTION("unexpected")
		{
			SECTION("const T &")
			{
				const T e{pal::unexpect, 2};
				CHECK(e.or_else(fe).error() == 4);
				CHECK(e.or_else(fv).error() == 2);
			}

			SECTION("T &")
			{
				T e{pal::unexpect, 2};
				CHECK(e.or_else(fe).error() == 4);
				CHECK(e.or_else(fv).error() == 2);
			}

			SECTION("const T &&")
			{
				const T e{pal::unexpect, 2};
				CHECK(std::move(e).or_else(fe).error() == 4);
				CHECK(std::move(e).or_else(fv).error() == 2);
			}

			SECTION("T &&")
			{
				T e{pal::unexpect, 2};
				CHECK(std::move(e).or_else(fe).error() == 4);
				CHECK(std::move(e).or_else(fv).error() == 2);
			}

			CHECK(fv_arg == 2);
		}
	}

	SECTION("chain")
	{
		using T = pal::expected<int, int>;

		auto ok = [&](int v) { return T{v * v}; };
		auto err = [&](int v) { return T{pal::unexpect, v}; };

		CHECK(T{2}.or_else(ok).or_else(ok).value() == 2);
		CHECK(T{2}.or_else(err).or_else(ok).value() == 2);
		CHECK(T{pal::unexpect, 2}.or_else(ok).or_else(err).value() == 4);
		CHECK(T{pal::unexpect, 2}.or_else(err).or_else(err).error() == 2);
	}
}


// expected<T, E>::map {{{1

TEST_CASE("expected::map")
{
	SECTION("int -> short")
	{
		using T = pal::expected<int, int>;
		using U = pal::expected<short, int>;
		auto f = [](int v) -> short { return short(v * v); };

		SECTION("expected")
		{
			SECTION("const T &")
			{
				const T e = 2;
				auto r = e.map(f);
				CHECK(r.value() == 4);
				static_assert(std::is_same_v<decltype(r), U>);
			}

			SECTION("T &")
			{
				T e = 2;
				auto r = e.map(f);
				CHECK(r.value() == 4);
				static_assert(std::is_same_v<decltype(r), U>);
			}

			SECTION("const T &&")
			{
				const T e = 2;
				auto r = std::move(e).map(f);
				CHECK(r.value() == 4);
				static_assert(std::is_same_v<decltype(r), U>);
			}

			SECTION("T &&")
			{
				T e = 2;
				auto r = std::move(e).map(f);
				CHECK(r.value() == 4);
				static_assert(std::is_same_v<decltype(r), U>);
			}
		}

		SECTION("unexpected")
		{
			SECTION("const T &")
			{
				const T e{pal::unexpect, 2};
				auto r = e.map(f);
				CHECK(r.error() == 2);
				static_assert(std::is_same_v<decltype(r), U>);
			}

			SECTION("T &")
			{
				T e{pal::unexpect, 2};
				auto r = e.map(f);
				CHECK(r.error() == 2);
				static_assert(std::is_same_v<decltype(r), U>);
			}

			SECTION("const T &&")
			{
				const T e{pal::unexpect, 2};
				auto r = std::move(e).map(f);
				CHECK(r.error() == 2);
				static_assert(std::is_same_v<decltype(r), U>);
			}

			SECTION("T &&")
			{
				T e{pal::unexpect, 2};
				auto r = std::move(e).map(f);
				CHECK(r.error() == 2);
				static_assert(std::is_same_v<decltype(r), U>);
			}
		}
	}

	SECTION("int -> void")
	{
		using T = pal::expected<int, int>;
		using U = pal::expected<void, int>;
		auto f = [](int) { (void)0; };

		SECTION("expected")
		{
			SECTION("const T &")
			{
				const T e = 2;
				auto r = e.map(f);
				CHECK(r.has_value());
				static_assert(std::is_same_v<decltype(r), U>);
			}

			SECTION("T &")
			{
				T e = 2;
				auto r = e.map(f);
				CHECK(r.has_value());
				static_assert(std::is_same_v<decltype(r), U>);
			}

			SECTION("const T &&")
			{
				const T e = 2;
				auto r = std::move(e).map(f);
				CHECK(r.has_value());
				static_assert(std::is_same_v<decltype(r), U>);
			}

			SECTION("T &&")
			{
				T e = 2;
				auto r = std::move(e).map(f);
				CHECK(r.has_value());
				static_assert(std::is_same_v<decltype(r), U>);
			}
		}

		SECTION("unexpected")
		{
			SECTION("const T &")
			{
				const T e{pal::unexpect, 2};
				auto r = e.map(f);
				CHECK(r.error() == 2);
				static_assert(std::is_same_v<decltype(r), U>);
			}

			SECTION("T &")
			{
				T e{pal::unexpect, 2};
				auto r = e.map(f);
				CHECK(r.error() == 2);
				static_assert(std::is_same_v<decltype(r), U>);
			}

			SECTION("const T &&")
			{
				const T e{pal::unexpect, 2};
				auto r = std::move(e).map(f);
				CHECK(r.error() == 2);
				static_assert(std::is_same_v<decltype(r), U>);
			}

			SECTION("T &&")
			{
				T e{pal::unexpect, 2};
				auto r = std::move(e).map(f);
				CHECK(r.error() == 2);
				static_assert(std::is_same_v<decltype(r), U>);
			}
		}
	}

	SECTION("void -> int")
	{
		using T = pal::expected<void, int>;
		using U = pal::expected<int, int>;
		auto f = []() -> int { return 2; };

		SECTION("expected")
		{
			SECTION("const T &")
			{
				const T e;
				auto r = e.map(f);
				CHECK(r.value() == 2);
				static_assert(std::is_same_v<decltype(r), U>);
			}

			SECTION("T &")
			{
				T e;
				auto r = e.map(f);
				CHECK(r.value() == 2);
				static_assert(std::is_same_v<decltype(r), U>);
			}

			SECTION("const T &&")
			{
				const T e;
				auto r = std::move(e).map(f);
				CHECK(r.value() == 2);
				static_assert(std::is_same_v<decltype(r), U>);
			}

			SECTION("T &&")
			{
				T e;
				auto r = std::move(e).map(f);
				CHECK(r.value() == 2);
				static_assert(std::is_same_v<decltype(r), U>);
			}
		}

		SECTION("unexpected")
		{
			SECTION("const T &")
			{
				const T e{pal::unexpect, 3};
				auto r = e.map(f);
				CHECK(r.error() == 3);
				static_assert(std::is_same_v<decltype(r), U>);
			}

			SECTION("T &")
			{
				T e{pal::unexpect, 3};
				auto r = e.map(f);
				CHECK(r.error() == 3);
				static_assert(std::is_same_v<decltype(r), U>);
			}

			SECTION("const T &&")
			{
				const T e{pal::unexpect, 3};
				auto r = std::move(e).map(f);
				CHECK(r.error() == 3);
				static_assert(std::is_same_v<decltype(r), U>);
			}

			SECTION("T &&")
			{
				T e{pal::unexpect, 3};
				auto r = std::move(e).map(f);
				CHECK(r.error() == 3);
				static_assert(std::is_same_v<decltype(r), U>);
			}
		}
	}

	SECTION("void -> void")
	{
		using T = pal::expected<void, int>;
		using U = pal::expected<void, int>;

		bool f_called = false;
		auto f = [&]() { f_called = true; };

		SECTION("expected")
		{
			SECTION("const T &")
			{
				const T e;
				f_called = false;
				auto r = e.map(f);
				CHECK(f_called);
				CHECK(r.has_value());
				static_assert(std::is_same_v<decltype(r), U>);
			}

			SECTION("T &")
			{
				T e;
				f_called = false;
				auto r = e.map(f);
				CHECK(f_called);
				CHECK(r.has_value());
				static_assert(std::is_same_v<decltype(r), U>);
			}

			SECTION("const T &&")
			{
				const T e;
				f_called = false;
				auto r = std::move(e).map(f);
				CHECK(f_called);
				CHECK(r.has_value());
				static_assert(std::is_same_v<decltype(r), U>);
			}

			SECTION("T &&")
			{
				const T e;
				f_called = false;
				auto r = std::move(e).map(f);
				CHECK(f_called);
				CHECK(r.has_value());
				static_assert(std::is_same_v<decltype(r), U>);
			}
		}

		SECTION("unexpected")
		{
			SECTION("const T &")
			{
				const T e{pal::unexpect, 3};
				f_called = false;
				auto r = e.map(f);
				CHECK(!f_called);
				CHECK(r.error() == 3);
				static_assert(std::is_same_v<decltype(r), U>);
			}

			SECTION("T &")
			{
				T e{pal::unexpect, 3};
				f_called = false;
				auto r = e.map(f);
				CHECK(!f_called);
				CHECK(r.error() == 3);
				static_assert(std::is_same_v<decltype(r), U>);
			}

			SECTION("const T &&")
			{
				const T e{pal::unexpect, 3};
				f_called = false;
				auto r = std::move(e).map(f);
				CHECK(!f_called);
				CHECK(r.error() == 3);
				static_assert(std::is_same_v<decltype(r), U>);
			}

			SECTION("T &&")
			{
				T e{pal::unexpect, 3};
				f_called = false;
				auto r = std::move(e).map(f);
				CHECK(!f_called);
				CHECK(r.error() == 3);
				static_assert(std::is_same_v<decltype(r), U>);
			}
		}
	}
}


// expected<T, E>::map_error {{{1

TEST_CASE("expected::map_error")
{
	SECTION("int -> short")
	{
		using T = pal::expected<int, int>;
		using U = pal::expected<int, short>;
		auto f = [](int v) -> short { return short(v * v); };

		SECTION("expected")
		{
			SECTION("const T &")
			{
				const T e = 2;
				auto r = e.map_error(f);
				CHECK(r.value() == 2);
				static_assert(std::is_same_v<decltype(r), U>);
			}

			SECTION("T &")
			{
				T e = 2;
				auto r = e.map_error(f);
				CHECK(r.value() == 2);
				static_assert(std::is_same_v<decltype(r), U>);
			}

			SECTION("const T &&")
			{
				const T e = 2;
				auto r = std::move(e).map_error(f);
				CHECK(r.value() == 2);
				static_assert(std::is_same_v<decltype(r), U>);
			}

			SECTION("T &&")
			{
				T e = 2;
				auto r = std::move(e).map_error(f);
				CHECK(r.value() == 2);
				static_assert(std::is_same_v<decltype(r), U>);
			}
		}

		SECTION("unexpected")
		{
			SECTION("const T &")
			{
				const T e{pal::unexpect, 2};
				auto r = e.map_error(f);
				CHECK(r.error() == 4);
				static_assert(std::is_same_v<decltype(r), U>);
			}

			SECTION("T &")
			{
				T e{pal::unexpect, 2};
				auto r = e.map_error(f);
				CHECK(r.error() == 4);
				static_assert(std::is_same_v<decltype(r), U>);
			}

			SECTION("const T &&")
			{
				const T e{pal::unexpect, 2};
				auto r = std::move(e).map_error(f);
				CHECK(r.error() == 4);
				static_assert(std::is_same_v<decltype(r), U>);
			}

			SECTION("T &&")
			{
				T e{pal::unexpect, 2};
				auto r = std::move(e).map_error(f);
				CHECK(r.error() == 4);
				static_assert(std::is_same_v<decltype(r), U>);
			}
		}
	}

	SECTION("int -> monostate")
	{
		using T = pal::expected<int, int>;
		using U = pal::expected<int, std::monostate>;
		auto f = [](int) { (void)0; };

		SECTION("expected")
		{
			SECTION("const T &")
			{
				const T e = 2;
				auto r = e.map_error(f);
				CHECK(r.value() == 2);
				static_assert(std::is_same_v<decltype(r), U>);
			}

			SECTION("T &")
			{
				T e = 2;
				auto r = e.map_error(f);
				CHECK(r.value() == 2);
				static_assert(std::is_same_v<decltype(r), U>);
			}

			SECTION("const T &&")
			{
				const T e = 2;
				auto r = std::move(e).map_error(f);
				CHECK(r.value() == 2);
				static_assert(std::is_same_v<decltype(r), U>);
			}

			SECTION("T &&")
			{
				T e = 2;
				auto r = std::move(e).map_error(f);
				CHECK(r.value() == 2);
				static_assert(std::is_same_v<decltype(r), U>);
			}
		}

		SECTION("unexpected")
		{
			SECTION("const T &")
			{
				const T e{pal::unexpect, 2};
				auto r = e.map_error(f);
				CHECK(!r.has_value());
				static_assert(std::is_same_v<decltype(r), U>);
			}

			SECTION("T &")
			{
				T e{pal::unexpect, 2};
				auto r = e.map_error(f);
				CHECK(!r.has_value());
				static_assert(std::is_same_v<decltype(r), U>);
			}

			SECTION("const T &&")
			{
				const T e{pal::unexpect, 2};
				auto r = std::move(e).map_error(f);
				CHECK(!r.has_value());
				static_assert(std::is_same_v<decltype(r), U>);
			}

			SECTION("T &&")
			{
				T e{pal::unexpect, 2};
				auto r = std::move(e).map_error(f);
				CHECK(!r.has_value());
				static_assert(std::is_same_v<decltype(r), U>);
			}
		}
	}

	SECTION("<void,int> -> <void,short>")
	{
		using T = pal::expected<void, int>;
		using U = pal::expected<void, short>;
		auto f = [](int v) -> short { return short(v * v); };

		SECTION("expected")
		{
			SECTION("const T &")
			{
				const T e;
				auto r = e.map_error(f);
				CHECK(r.has_value());
				static_assert(std::is_same_v<decltype(r), U>);
			}

			SECTION("T &")
			{
				T e;
				auto r = e.map_error(f);
				CHECK(r.has_value());
				static_assert(std::is_same_v<decltype(r), U>);
			}

			SECTION("const T &&")
			{
				const T e;
				auto r = std::move(e).map_error(f);
				CHECK(r.has_value());
				static_assert(std::is_same_v<decltype(r), U>);
			}

			SECTION("T &&")
			{
				T e;
				auto r = std::move(e).map_error(f);
				CHECK(r.has_value());
				static_assert(std::is_same_v<decltype(r), U>);
			}
		}

		SECTION("unexpected")
		{
			SECTION("const T &")
			{
				const T e{pal::unexpect, 2};
				auto r = e.map_error(f);
				CHECK(r.error() == 4);
				static_assert(std::is_same_v<decltype(r), U>);
			}

			SECTION("T &")
			{
				T e{pal::unexpect, 2};
				auto r = e.map_error(f);
				CHECK(r.error() == 4);
				static_assert(std::is_same_v<decltype(r), U>);
			}

			SECTION("const T &&")
			{
				const T e{pal::unexpect, 2};
				auto r = std::move(e).map_error(f);
				CHECK(r.error() == 4);
				static_assert(std::is_same_v<decltype(r), U>);
			}

			SECTION("T &&")
			{
				T e{pal::unexpect, 2};
				auto r = std::move(e).map_error(f);
				CHECK(r.error() == 4);
				static_assert(std::is_same_v<decltype(r), U>);
			}
		}
	}

	SECTION("<void,int> -> <void,std::monostate>")
	{
		using T = pal::expected<void, int>;
		using U = pal::expected<void, std::monostate>;

		bool f_called = false;
		auto f = [&](int) { f_called = true; };

		SECTION("expected")
		{
			SECTION("const T &")
			{
				const T e;
				f_called = false;
				auto r = e.map_error(f);
				CHECK(!f_called);
				CHECK(r.has_value());
				static_assert(std::is_same_v<decltype(r), U>);
			}

			SECTION("T &")
			{
				T e;
				f_called = false;
				auto r = e.map_error(f);
				CHECK(!f_called);
				CHECK(r.has_value());
				static_assert(std::is_same_v<decltype(r), U>);
			}

			SECTION("const T &&")
			{
				const T e;
				f_called = false;
				auto r = std::move(e).map_error(f);
				CHECK(!f_called);
				CHECK(r.has_value());
				static_assert(std::is_same_v<decltype(r), U>);
			}

			SECTION("T &&")
			{
				const T e;
				f_called = false;
				auto r = std::move(e).map_error(f);
				CHECK(!f_called);
				CHECK(r.has_value());
				static_assert(std::is_same_v<decltype(r), U>);
			}
		}

		SECTION("unexpected")
		{
			SECTION("const T &")
			{
				const T e{pal::unexpect, 2};
				f_called = false;
				auto r = e.map_error(f);
				CHECK(f_called);
				CHECK(!r.has_value());
				static_assert(std::is_same_v<decltype(r), U>);
			}

			SECTION("T &")
			{
				T e{pal::unexpect, 2};
				f_called = false;
				auto r = e.map_error(f);
				CHECK(f_called);
				CHECK(!r.has_value());
				static_assert(std::is_same_v<decltype(r), U>);
			}

			SECTION("const T &&")
			{
				const T e{pal::unexpect, 2};
				f_called = false;
				auto r = std::move(e).map_error(f);
				CHECK(f_called);
				CHECK(!r.has_value());
				static_assert(std::is_same_v<decltype(r), U>);
			}

			SECTION("T &&")
			{
				T e{pal::unexpect, 2};
				f_called = false;
				auto r = std::move(e).map_error(f);
				CHECK(f_called);
				CHECK(!r.has_value());
				static_assert(std::is_same_v<decltype(r), U>);
			}
		}
	}
}


// expected<[non_]trivial, [non_]trivial> //{{{1

using trivial_trivial = pal::expected<trivial_type, trivial_type>;
using trivial_non_trivial = pal::expected<trivial_type, non_trivial_type>;
using non_trivial_trivial = pal::expected<non_trivial_type, trivial_type>;
using non_trivial_non_trivial = pal::expected<non_trivial_type, non_trivial_type>;

TEMPLATE_TEST_CASE("expected", "",
	trivial_trivial,
	trivial_non_trivial,
	non_trivial_trivial,
	non_trivial_non_trivial)
{
	using T = typename TestType::value_type;
	const auto v1 = value<T>();
	const auto v2 = other_value(v1);
	using U = std::remove_cvref_t<decltype(v2)>;

	using E = typename TestType::error_type;
	const auto e1 = value<E>();
	const auto e2 = other_value(e1);
	using G = std::remove_cvref_t<decltype(e2)>;

	using other_t = pal::expected<U, G>;

	SECTION("expected()")
	{
		TestType x;
		CHECK(x.value() == T{});
	}

	SECTION("expected(const expected &)")
	{
		SECTION("x")
		{
			TestType x{v1}, y{x};
			CHECK(y.value() == v1);
		}

		SECTION("!x")
		{
			TestType x{pal::unexpect, e1}, y{x};
			CHECK(y.error() == e1);
		}
	}

	SECTION("expected(expected &&)")
	{
		SECTION("x")
		{
			TestType x, y{std::move(x)};
			CHECK(y.value() == T{});
		}

		SECTION("!x")
		{
			TestType x{pal::unexpect, e1}, y{std::move(x)};
			CHECK(y.error() == e1);
		}
	}

	SECTION("expected(const expected<U, G> &)")
	{
		SECTION("x")
		{
			other_t x{v2};
			TestType y{x};
			CHECK(y.value() == v2);
		}

		SECTION("!x")
		{
			other_t x{pal::unexpect, e2};
			TestType y{x};
			CHECK(y.error() == e2);
		}
	}

	SECTION("expected(expected<U, G> &&)")
	{
		SECTION("x")
		{
			other_t x{v2};
			TestType y{std::move(x)};
			CHECK(y.value() == v2);
		}

		SECTION("!x")
		{
			other_t x{pal::unexpect, e2};
			TestType y{std::move(x)};
			CHECK(y.error() == e2);
		}
	}

	SECTION("expected(U &&)")
	{
		TestType x{v2};
		CHECK(x.value() == v2);
	}

	SECTION("expected(const unexpected<E> &)")
	{
		pal::unexpected<E> e{std::in_place};
		TestType x{e};
		CHECK(x.error() == E{});
	}

	SECTION("expected(const unexpected<G> &)")
	{
		pal::unexpected<G> e{std::in_place};
		TestType x{e};
		CHECK(x.error() == E{});
	}

	SECTION("expected(unexpected<E> &&)")
	{
		pal::unexpected<E> e{std::in_place};
		TestType x{std::move(e)};
		CHECK(x.error() == E{});
	}

	SECTION("expected(unexpected<G> &&)")
	{
		pal::unexpected<G> e{std::in_place};
		TestType x{std::move(e)};
		CHECK(x.error() == E{});
	}

	SECTION("expected(in_place)")
	{
		TestType x{std::in_place, v1};
		CHECK(x.value() == v1);
	}

	SECTION("expected(unexpect)")
	{
		TestType x{pal::unexpect, e1};
		CHECK(x.error() == e1);
	}

	SECTION("operator=(const expected &)")
	{
		SECTION("x && y")
		{
			TestType x, y{v1};
			x = y;
			CHECK(x.value() == v1);
		}

		SECTION("!x && y")
		{
			TestType x{pal::unexpect, e1}, y{v1};
			x = y;
			CHECK(x.value() == v1);
		}

		SECTION("x && !y")
		{
			TestType x, y{pal::unexpect, e1};
			x = y;
			CHECK(x.error() == e1);
		}

		SECTION("!x && !y")
		{
			TestType x{pal::unexpect, e1}, y{pal::unexpect, e2};
			x = y;
			CHECK(x.error() == e2);
		}
	}

	SECTION("operator=(expected &&)")
	{
		SECTION("x && y")
		{
			TestType x, y{v1};
			x = std::move(y);
			CHECK(x.value() == v1);
		}

		SECTION("!x && y")
		{
			TestType x{pal::unexpect, e1}, y{v1};
			x = std::move(y);
			CHECK(x.value() == v1);
		}

		SECTION("x && !y")
		{
			TestType x, y{pal::unexpect, e1};
			x = std::move(y);
			CHECK(x.error() == e1);
		}

		SECTION("!x && !y")
		{
			TestType x{pal::unexpect, e1}, y{pal::unexpect, e2};
			x = std::move(y);
			CHECK(x.error() == e2);
		}
	}

	SECTION("operator=(const unexpected<G> &)")
	{
		SECTION("x")
		{
			TestType x;
			pal::unexpected<G> y{e2};
			x = y;
			CHECK(x.error() == e2);
		}

		SECTION("!x")
		{
			TestType x{pal::unexpect, e1};
			pal::unexpected<G> y{e2};
			x = y;
			CHECK(x.error() == e2);
		}
	}

	SECTION("operator=(unexpected<G> &&)")
	{
		SECTION("x")
		{
			TestType x;
			pal::unexpected<G> y{e2};
			x = std::move(y);
			CHECK(x.error() == e2);
		}

		SECTION("!x")
		{
			TestType x{pal::unexpect, e1};
			pal::unexpected<G> y{e2};
			x = std::move(y);
			CHECK(x.error() == e2);
		}
	}

	SECTION("operator=(U &&)")
	{
		SECTION("x")
		{
			TestType x{v1};
			x = std::move(v2);
			CHECK(x.value() == v2);
		}

		SECTION("!x")
		{
			TestType x{pal::unexpect, e1};
			x = std::move(v2);
			CHECK(x.value() == v2);
		}
	}

	SECTION("emplace")
	{
		SECTION("x")
		{
			TestType x{v1};
			CHECK(x.emplace(v2) == v2);
			CHECK(x.value() == v2);
		}

		SECTION("!x")
		{
			TestType x{pal::unexpect, e1};
			CHECK(x.emplace(v2) == v2);
			CHECK(x.value() == v2);
		}
	}
}


// expected<void, [non_]trivial> //{{{1

using void_trivial = pal::expected<void, trivial_type>;
using void_non_trivial = pal::expected<void, non_trivial_type>;

TEMPLATE_TEST_CASE("expected", "",
	void_trivial,
	void_non_trivial)
{
	using E = typename TestType::error_type;
	const auto e1 = value<E>();
	const auto e2 = other_value(e1);
	using G = std::remove_cvref_t<decltype(e2)>;

	using other_t = pal::expected<void, G>;
	other_t unused;
	(void)unused;

	SECTION("expected()")
	{
		TestType x;
		CHECK(x);
	}

	SECTION("expected(const expected &)")
	{
		SECTION("x")
		{
			TestType x, y{x};
			CHECK(y);
		}

		SECTION("!x")
		{
			TestType x{pal::unexpect, e1}, y{x};
			CHECK(y.error() == e1);
		}
	}

	SECTION("expected(expected &&)")
	{
		SECTION("x")
		{
			TestType x, y{std::move(x)};
			CHECK(y);
		}

		SECTION("!x")
		{
			TestType x{pal::unexpect, e1}, y{std::move(x)};
			CHECK(y.error() == e1);
		}
	}

	SECTION("expected(const expected<U, G> &)")
	{
		SECTION("x")
		{
			other_t x;
			TestType y{x};
			CHECK(y);
		}

		SECTION("!x")
		{
			other_t x{pal::unexpect, e2};
			TestType y{x};
			CHECK(y.error() == e2);
		}
	}

	SECTION("expected(expected<U, G> &&)")
	{
		SECTION("x")
		{
			other_t x;
			TestType y{std::move(x)};
			CHECK(y);
		}

		SECTION("!x")
		{
			other_t x{pal::unexpect, e2};
			TestType y{std::move(x)};
			CHECK(y.error() == e2);
		}
	}

	SECTION("expected(const unexpected<E> &)")
	{
		pal::unexpected<E> e{std::in_place};
		TestType x{e};
		CHECK(x.error() == E{});
	}

	SECTION("expected(const unexpected<G> &)")
	{
		pal::unexpected<G> e{std::in_place};
		TestType x{e};
		CHECK(x.error() == E{});
	}

	SECTION("expected(unexpected<E> &&)")
	{
		pal::unexpected<E> e{std::in_place};
		TestType x{std::move(e)};
		CHECK(x.error() == E{});
	}

	SECTION("expected(unexpected<G> &&)")
	{
		pal::unexpected<G> e{std::in_place};
		TestType x{std::move(e)};
		CHECK(x.error() == E{});
	}

	SECTION("expected(in_place)")
	{
		TestType x{std::in_place};
		CHECK(x);
	}

	SECTION("expected(unexpect)")
	{
		TestType x{pal::unexpect, e1};
		CHECK(x.error() == e1);
	}

	SECTION("operator=(const expected &)")
	{
		SECTION("x && y")
		{
			TestType x, y;
			x = y;
			CHECK(x);
		}

		SECTION("!x && y")
		{
			TestType x{pal::unexpect, e1}, y;
			x = y;
			CHECK(x);
		}

		SECTION("x && !y")
		{
			TestType x, y{pal::unexpect, e1};
			x = y;
			CHECK(!x);
		}

		SECTION("!x && !y")
		{
			TestType x{pal::unexpect, e1}, y{pal::unexpect, e1};
			x = y;
			CHECK(!x);
		}
	}

	SECTION("operator=(expected &&)")
	{
		SECTION("x && y")
		{
			TestType x, y;
			x = std::move(y);
			CHECK(x);
		}

		SECTION("!x && y")
		{
			TestType x{pal::unexpect, e1}, y;
			x = std::move(y);
			CHECK(x);
		}

		SECTION("x && !y")
		{
			TestType x, y{pal::unexpect, e1};
			x = std::move(y);
			CHECK(!x);
		}

		SECTION("!x && !y")
		{
			TestType x{pal::unexpect, e1}, y{pal::unexpect, e1};
			x = std::move(y);
			CHECK(!x);
		}
	}

	SECTION("operator=(const unexpected<G> &)")
	{
		SECTION("x")
		{
			pal::unexpected<G> y{e2};
			TestType x;
			x = y;
			CHECK(x.error() == e2);
		}

		SECTION("!x")
		{
			pal::unexpected<G> y{e2};
			TestType x{pal::unexpect, e1};
			x = y;
			CHECK(x.error() == e2);
		}
	}

	SECTION("operator=(unexpected<G> &&)")
	{
		SECTION("x")
		{
			pal::unexpected<G> y{e2};
			TestType x;
			x = std::move(y);
			CHECK(x.error() == e2);
		}

		SECTION("!x")
		{
			pal::unexpected<G> y{e2};
			TestType x{pal::unexpect, e1};
			x = std::move(y);
			CHECK(x.error() == e2);
		}
	}

	SECTION("emplace")
	{
		SECTION("x")
		{
			TestType x;
			x.emplace();
			CHECK(x);
		}

		SECTION("!x")
		{
			TestType x{pal::unexpect, e1};
			x.emplace();
			CHECK(x);
		}
	}
}

// }}}1

} // namespace


#if defined(_MSC_BUILD) && !defined(_DEBUG)
	#pragma warning(pop)
#endif
