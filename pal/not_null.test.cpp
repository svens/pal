#include <pal/not_null>
#include <pal/test>
#include <catch2/catch_template_test_macros.hpp>

namespace {

struct base
{
	static inline int instances = 0;

	base ()
	{
		instances++;
	}

	virtual ~base ()
	{
		instances--;
	}

	virtual bool test () const
	{
		return false;
	}
};

struct derived: base
{
	bool test () const override
	{
		return true;
	}
};

void check_const_ref (const base *ptr)
{
	CHECK(ptr->test() == true);
}

void check_const_ref (const std::unique_ptr<base> &ptr)
{
	CHECK(ptr->test() == true);
}

void check_const_ref (const std::shared_ptr<base> &ptr)
{
	CHECK(ptr->test() == true);
}

void check_rvalue (base *&&ptr)
{
	CHECK(ptr->test() == true);
}

void check_rvalue (std::unique_ptr<base> &&ptr)
{
	CHECK(ptr->test() == true);
}

void check_rvalue (std::shared_ptr<base> &&ptr)
{
	CHECK(ptr->test() == true);
}

TEMPLATE_TEST_CASE("not_null", "",
	base *,
	std::unique_ptr<base>,
	std::shared_ptr<base>)
{
	using not_null_TestType = pal::not_null<TestType>;
	using element_type = typename not_null_TestType::element_type;

	derived * const plain_ptr = new derived;
	TestType ptr{plain_ptr};
	CHECK(base::instances == 1);

	SECTION("traits")
	{
		CHECK_FALSE(pal::is_not_null_v<TestType>);
		CHECK(pal::is_not_null_v<not_null_TestType>);
		CHECK(std::is_same_v<base, element_type>);
	}

	if constexpr (!std::is_same_v<std::unique_ptr<base>, TestType>)
	{
		SECTION("not_null(const Ptr &)")
		{
			auto not_null_ptr = not_null_TestType{ptr};
			CHECK(not_null_ptr.get() == plain_ptr);
		}
	}

	SECTION("not_null(Ptr &&)")
	{
		auto not_null_ptr = not_null_TestType{std::move(ptr)};
		CHECK(not_null_ptr.get() == plain_ptr);
	}

	SECTION("operator->")
	{
		auto not_null_ptr = not_null_TestType{std::move(ptr)};
		CHECK(not_null_ptr->test());
	}

	SECTION("operator*")
	{
		auto not_null_ptr = not_null_TestType{std::move(ptr)};
		CHECK((*not_null_ptr).test());
	}

	SECTION("operator const Ptr &")
	{
		auto not_null_ptr = not_null_TestType{std::move(ptr)};
		check_const_ref(not_null_ptr);
	}

	SECTION("operator Ptr &&")
	{
		auto not_null_ptr = not_null_TestType{std::move(ptr)};
		check_rvalue(std::move(not_null_ptr));
		check_rvalue(std::move(not_null_ptr));
	}

	SECTION("const Ptr &as_nullable")
	{
		auto not_null_ptr = not_null_TestType{std::move(ptr)};
		check_const_ref(not_null_ptr.as_nullable());
	}

	SECTION("Ptr &&as_nullable")
	{
		auto not_null_ptr = not_null_TestType{std::move(ptr)};
		check_rvalue(std::move(not_null_ptr).as_nullable());
	}

	SECTION("to_address")
	{
		auto not_null_ptr = not_null_TestType{std::move(ptr)};
		CHECK(std::to_address(not_null_ptr) == plain_ptr);
	}

	if constexpr (std::is_same_v<base *, TestType>)
	{
		delete ptr;
		ptr = nullptr;
	}
	else
	{
		ptr.reset();
	}
	CHECK(base::instances == 0);

	SECTION("nullptr")
	{
		if constexpr (pal::build == pal::build_type::debug)
		{
			REQUIRE_THROWS_AS(not_null_TestType{std::move(ptr)}, std::logic_error);
		}
		else
		{
			// not ideal but what can we do?
			auto p = not_null_TestType{std::move(ptr)};
			CHECK(p.get() == nullptr);
		}
	}
}

template <typename T>
struct test_explicit_move_ptr: std::unique_ptr<T>
{
	explicit test_explicit_move_ptr (std::unique_ptr<T> &&that)
		: std::unique_ptr<T>(std::forward<std::unique_ptr<T>>(that))
	{}
};

TEST_CASE("not_null")
{
	SECTION("explicit not_null(const U &)")
	{
		CHECK(base::instances == 0);
		pal::not_null up = new derived;
		pal::not_null<std::unique_ptr<base>> sp{up};
		CHECK(base::instances == 1);
	}

	SECTION("explicit not_null(U &&)")
	{
		CHECK(base::instances == 0);
		pal::not_null up = std::make_unique<derived>();
		pal::not_null<test_explicit_move_ptr<base>> sp{std::move(up)};
		CHECK(base::instances == 1);
	}

	SECTION("not_null(const U &)")
	{
		CHECK(base::instances == 0);
		pal::not_null up = std::make_shared<derived>();
		pal::not_null<std::shared_ptr<base>> sp{up};
		CHECK(base::instances == 1);
	}

	SECTION("not_null(U &&)")
	{
		CHECK(base::instances == 0);
		pal::not_null up = std::make_unique<derived>();
		pal::not_null<std::shared_ptr<base>> sp{std::move(up)};
		CHECK(base::instances == 1);
	}

	CHECK(base::instances == 0);

	SECTION("comparisons")
	{
		base i[2];
		pal::not_null<base *> i_0{&i[0]}, i_0_1{&i[0]}, i_1{&i[1]};
		CHECK(i_0 == i_0_1);
		CHECK(i_0 != i_1);
		CHECK(i_0 < i_1);
		CHECK(i_0 <= i_0_1);
		CHECK(i_0 <= i_1);
		CHECK(i_1 > i_0_1);
		CHECK(i_1 >= i_0);
		CHECK(i_0 >= i_0_1);
	}
}

} // namespace
