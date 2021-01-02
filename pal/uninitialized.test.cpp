#include <pal/uninitialized>
#include <pal/test>


namespace {


struct foo
{
	static inline size_t instances = 0;
	size_t value{};

	foo ()
		: value{instances++}
	{ }

	foo (bool v)
	{
		throw v;
	}

	~foo ()
	{
		instances--;
	}

	int fn () const & noexcept
	{
		return 1;
	}

	int fn () & noexcept
	{
		return 2;
	}

	int fn () const && noexcept
	{
		return 3;
	}

	int fn () && noexcept
	{
		return 4;
	}
};


TEST_CASE("uninitialized - foo")
{
	using T = pal::uninitialized<foo>;

	CHECK(foo::instances == 0);
	T u;
	CHECK(foo::instances == 0);

	SECTION("construct")
	{
		u.construct();
		CHECK(foo::instances == 1);
		foo::instances = 0;
	}

	SECTION("destruct")
	{
		u.construct();
		CHECK(foo::instances == 1);
		u.destruct();
		CHECK(foo::instances == 0);
	}

	SECTION("const T &")
	{
		u.construct();
		CHECK(static_cast<const T &>(u)->fn() == 1);
		CHECK((*static_cast<const T &>(u)).fn() == 1);
		CHECK(static_cast<const T &>(u).value().fn() == 1);
		u.destruct();
	}

	SECTION("T &")
	{
		u.construct();
		CHECK(u->fn() == 2);
		CHECK((*u).fn() == 2);
		CHECK(u.value().fn() == 2);
		u.destruct();
	}

	SECTION("const T &&")
	{
		u.construct();
		CHECK((*static_cast<const T &&>(std::move(u))).fn() == 3);
		CHECK(static_cast<const T &&>(std::move(u)).value().fn() == 3);
		u.destruct();
	}

	SECTION("T &&")
	{
		u.construct();
		CHECK((*std::move(u)).fn() == 4);
		CHECK(std::move(u).value().fn() == 4);
		u.destruct();
	}

	SECTION("throwing construct")
	{
		CHECK_THROWS_AS(
			u.construct(true),
			bool
		);
	}

	CHECK(foo::instances == 0);
}


using trivial_type = int;
using non_trivial_type = std::string;


TEMPLATE_TEST_CASE("uninitialized", "",
	trivial_type,
	non_trivial_type)
{
	TestType value{};

	using T = pal::uninitialized<TestType>;
	T u;
	u.construct(value);

	SECTION("operator->")
	{
		CHECK((const void *)static_cast<const T &>(u).operator->() == (const void *)&u.data);
		CHECK((void *)u.operator->() == (void *)&u.data);
	}

	SECTION("operator*")
	{
		CHECK(*static_cast<const T &>(u) == value);
		CHECK(*u == value);
		CHECK(*static_cast<const T &&>(std::move(u)) == value);
		CHECK(*std::move(u) == value);
	}

	SECTION("value")
	{
		CHECK(static_cast<const T &>(u).value() == value);
		CHECK(u.value() == value);
		CHECK(static_cast<const T &&>(std::move(u)).value() == value);
		CHECK(std::move(u).value() == value);
	}

	u.destruct();
}


} // namespace
