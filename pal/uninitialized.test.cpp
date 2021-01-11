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
		constexpr pal::uninitialized<int> u{pal::no_init};
		(void)u;

		constexpr pal::uninitialized<int> u1;
		constexpr auto v1 = u1.get();
		static_assert(*u1 == v1);
		static_assert(*(u1.operator->()) == v1);

		constexpr pal::uninitialized u2{1};
		constexpr auto v2 = u2.get();
		static_assert(*u2 == v2);
		static_assert(*(u2.operator->()) == v2);
	}


	CHECK(object_counter::instances == 0);
	pal::uninitialized<object_counter> u{pal::no_init};
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
T value_1 ()
{
	return 1;
}


template <>
std::string value_1<std::string> ()
{
	return pal_test::case_name();
}


TEMPLATE_TEST_CASE("uninitialized", "",
	trivial_type,
	non_trivial_type)
{
	pal::uninitialized<TestType> def;
	CHECK(def.get() == TestType{});

	auto v1 = value_1<TestType>();
	pal::uninitialized u{v1};
	using T = decltype(u);

	SECTION("sizeof")
	{
		CHECK(sizeof(u) == sizeof(TestType));
	}

	SECTION("addressof")
	{
		CHECK((void *)std::addressof(u) == (void *)std::addressof(u.get()));
	}

	SECTION("construct")
	{
		pal::uninitialized<TestType> empty{pal::no_init};
		empty.construct();
		CHECK(empty.get() == def.get());
		empty.destruct();
	}

	SECTION("construct / destruct")
	{
		u.destruct();
		u.construct(def.get());
		CHECK(u.get() == def.get());
	}

	SECTION("assign(const T &)")
	{
		u.assign(def.get());
		CHECK(u.get() == def.get());
	}

	SECTION("assign(T &&)")
	{
		auto v = def.get();
		u.assign(std::move(v));
		CHECK(u.get() == def.get());
	}

	SECTION("get")
	{
		CHECK(static_cast<const T &>(u).get() == v1);
		CHECK(u.get() == v1);
		CHECK(static_cast<const T &&>(std::move(u)).get() == v1);
		CHECK(std::move(u).get() == v1);
	}

	SECTION("operator*")
	{
		CHECK(*static_cast<const T &>(u) == v1);
		CHECK(*u == v1);
		CHECK(*static_cast<const T &&>(std::move(u)) == v1);
		CHECK(*std::move(u) == v1);
	}

	SECTION("operator->")
	{
		CHECK(static_cast<const T &>(u).operator->() == (void *)&u);
		CHECK(u.operator->() == (void *)&u);
	}

	def.destruct();
	u.destruct();
}


} // namespace
