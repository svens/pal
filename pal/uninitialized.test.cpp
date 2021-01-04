#include <pal/uninitialized>
#include <pal/test>


namespace {


struct object_counter
{
	static inline size_t instances = 0;

	object_counter ()
	{
		instances++;
	}

	~object_counter ()
	{
		instances--;
	}
};


TEST_CASE("uninitialized")
{
	SECTION("constexpr")
	{
		constexpr pal::uninitialized u1{1};
		constexpr auto v1 = u1.get();
		CHECK(u1.get() == v1);

		constexpr pal::uninitialized u2{u1};
		constexpr auto v2 = *u2;
		CHECK(u2.get() == v2);

		constexpr pal::uninitialized u3{std::move(u2)};
		constexpr auto v3 = *u3.operator->();
		CHECK(u3.get() == v3);
	}

	using T = pal::uninitialized<object_counter>;

	CHECK(object_counter::instances == 0);
	T u;
	CHECK(object_counter::instances == 0);

	SECTION("construct")
	{
		u.construct();
		CHECK(object_counter::instances == 1);
		object_counter::instances = 0;
	}

	SECTION("destruct")
	{
		u.construct();
		CHECK(object_counter::instances == 1);
		u.destruct();
		CHECK(object_counter::instances == 0);
	}
}


using trivial_type = int;
using non_trivial_type = std::string;


template <typename T>
T value ()
{
	return {};
}


template <>
std::string value<std::string> ()
{
	return pal_test::case_name();
}


TEMPLATE_TEST_CASE("uninitialized", "",
	trivial_type,
	non_trivial_type)
{
	auto v = value<TestType>();

	using T = pal::uninitialized<TestType>;
	T u{v};

	SECTION("sizeof")
	{
		CHECK(sizeof(u) == sizeof(TestType));
	}

	SECTION("addressof")
	{
		CHECK((void *)std::addressof(u) == (void *)std::addressof(u.get()));
	}

	SECTION("uninitialized(const uninitialized &)")
	{
		auto x{u};
		CHECK(x.get() == v);
	}

	SECTION("uninitialized(uninitialized &&)")
	{
		auto x{std::move(u)};
		CHECK(x.get() == v);
	}

	SECTION("construct(const uninitialized &)")
	{
		T x;
		x.construct(u);
		CHECK(x.get() == v);
	}

	SECTION("construct(uninitialized &&)")
	{
		T x;
		x.construct(std::move(u));
		CHECK(x.get() == v);
	}

	SECTION("assign(const uninitialized &)")
	{
		T x{u};
		x.assign(u);
		CHECK(x.get() == v);
	}

	SECTION("assign(uninitialized &&)")
	{
		T x{u};
		x.assign(std::move(u));
		CHECK(x.get() == v);
	}

	SECTION("get")
	{
		CHECK(static_cast<const T &>(u).get() == v);
		CHECK(u.get() == v);
		CHECK(static_cast<const T &&>(std::move(u)).get() == v);
		CHECK(std::move(u).get() == v);
	}

	SECTION("operator*")
	{
		CHECK(*static_cast<const T &>(u) == v);
		CHECK(*u == v);
		CHECK(*static_cast<const T &&>(std::move(u)) == v);
		CHECK(*std::move(u) == v);
	}

	SECTION("operator->")
	{
		CHECK(static_cast<const T &>(u).operator->() == (void *)&u);
		CHECK(u.operator->() == (void *)&u);
	}
}


} // namespace
