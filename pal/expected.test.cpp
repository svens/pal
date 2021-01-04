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


// unexpected {{{1


TEST_CASE("unexpected")
{
	using TestType = int;

	SECTION("constexpr")
	{
		constexpr TestType value{1};

		constexpr pal::unexpected x{value};
		CHECK(x.value() == value);

		constexpr pal::unexpected y{std::in_place, value};
		CHECK(y.value() == value);

		constexpr pal::unexpected w{x};
		CHECK(w.value() == value);

		constexpr pal::unexpected z{std::move(y)};
		CHECK(z.value() == value);
	}
}


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


// expected<T, E> {{{1

struct copy_and_move //{{{2
{
	int base{1};

	copy_and_move () = default;
	~copy_and_move () = default;

	copy_and_move (const copy_and_move &) = default;
	copy_and_move (copy_and_move &&) = default;
	copy_and_move &operator= (const copy_and_move &) = default;
	copy_and_move &operator= (copy_and_move &&) = default;

	constexpr copy_and_move (int v) noexcept
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


struct copy_only: public copy_and_move //{{{2
{
	using copy_and_move::copy_and_move;
	using copy_and_move::operator=;

	copy_only (const copy_only &) = default;
	copy_only &operator= (const copy_only &) = default;

	copy_only (copy_only &&) = delete;
	copy_only &operator= (copy_only &&) = delete;
};


struct move_only: public copy_and_move //{{{2
{
	using copy_and_move::copy_and_move;
	using copy_and_move::operator=;

	move_only (move_only &&) = default;
	move_only &operator= (move_only &&) = default;

	move_only (const move_only &) = delete;
	move_only &operator= (const move_only &) = delete;
};


struct no_copy_or_move: public copy_and_move //{{{2
{
	using copy_and_move::copy_and_move;
	using copy_and_move::operator=;

	no_copy_or_move (const no_copy_or_move &) = delete;
	no_copy_or_move &operator= (const no_copy_or_move &) = delete;

	no_copy_or_move (no_copy_or_move &&) = delete;
	no_copy_or_move &operator= (no_copy_or_move &&) = delete;
};


//}}}2


TEMPLATE_TEST_CASE("expected", "",
	no_copy_or_move,
	copy_and_move,
	copy_only,
	move_only,
	void)
{
	using T = pal::expected<TestType, int>;
	using other_t = pal::expected<int, short>;

	//
	// Constructors
	//

#if 0
	SECTION("constexpr")
	{
		constexpr T e;
		CHECK(e.has_value());

		if constexpr (!std::is_void_v<TestType> && e)
		{
			constexpr auto v1 = e->base;
			CHECK(v1 == 1);

			constexpr auto v2 = (*e).base;
			CHECK(v2 == 1);

			constexpr auto v3 = e.value().base;
			CHECK(v3 == 1);

			if constexpr (std::is_copy_constructible_v<TestType>)
			{
				constexpr auto v4 = e.value_or(2).base;
				CHECK(v4 == 1);
			}

			/* "T value_or(U&&) &&" is not const
			if constexpr (std::is_move_constructible_v<TestType>)
			{
				constexpr auto v5 = std::move(e).value_or(2).base;
				CHECK(v5 == 1);
			}
			*/
		}
	}
#endif

	SECTION("expected()")
	{
		T e;
		CHECK(bool(e));
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

			T u{pal::unexpect, true};
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

			T u{pal::unexpect, true};
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
		CHECK(!bool(e));
		CHECK(e.error() == 100);
	}

	SECTION("expected(const unexpected<G> &)")
	{
		const pal::unexpected<char> u{'\1'};
		T e{u};
		CHECK(!bool(e));
		CHECK(e.error() == 1);
	}

	SECTION("expected(unexpected<E> &&)")
	{
		pal::unexpected u{short(100)};
		T e{std::move(u)};
		CHECK(!bool(e));
		CHECK(e.error() == 100);
	}

	SECTION("expected(unexpected<G> &&)")
	{
		pal::unexpected<char> u{'\1'};
		T e{std::move(u)};
		CHECK(!bool(e));
		CHECK(e.error() == 1);
	}

	SECTION("expected(in_place)")
	{
		if constexpr (std::is_void_v<TestType>)
		{
			T e{std::in_place};
			CHECK(bool(e));
		}
		else
		{
			T e{std::in_place, 2};
			CHECK(bool(e));
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
				CHECK(x.has_value());
			}

			SECTION("!x && y")
			{
				T x{pal::unexpect, 100}, y;
				x = y;
				CHECK(x.has_value());
			}

			SECTION("x && !y")
			{
				T x, y{pal::unexpect, 100};
				x = y;
				CHECK_FALSE(x.has_value());
			}

			SECTION("!x && !y")
			{
				T x{pal::unexpect, false}, y{pal::unexpect, 100};
				x = y;
				CHECK_FALSE(x.has_value());
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
		CHECK(bool(e));
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
				}
			}

			SECTION("T value_or (U &&v) &&")
			{
				if constexpr (std::is_move_constructible_v<TestType>)
				{
					CHECK(std::move(e).value_or(0).fn() == 4);
				}
			}
		}
	}

	//
	// Equality
	//

	//
	// Comparison with T
	//

	//
	// Comparison with unexpected<E>
	//
}


// expected<T, E>::swap {{{1


TEST_CASE("expected::swap")
{
	using std::swap;

	using move_ctor_can_throw_t = pal_test::throwable<pal_test::move_ctor_can_throw>;
	using move_ctor_throws_t = pal_test::throwable<pal_test::move_ctor_throws>;

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

// }}}1


} // namespace


#if defined(_MSC_BUILD) && !defined(_DEBUG)
	#pragma warning(pop)
#endif
