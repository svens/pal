#include <pal/task>
#include <pal/test>


namespace {


void foo (pal::task &arg) noexcept
{
	*static_cast<int *>(arg.user_data()) = 1;
}


void bar (pal::task &arg) noexcept
{
	*static_cast<int *>(arg.user_data()) = 2;
}


TEST_CASE("task")
{
	int arg = 0;
	pal::task f1{&foo, &arg}, f2{&foo}, b{&bar, &arg};

	SECTION("no function")
	{
		if constexpr (!pal::expect_noexcept)
		{
			void(*null)(pal::task &)noexcept = {};
			CHECK_THROWS_AS(
				pal::task{null},
				std::logic_error
			);
		}
	}

	SECTION("user_data")
	{
		CHECK(f1.user_data() == &arg);
		CHECK(f1 == f2);
		CHECK(f1.user_data() != f2.user_data());

		f2.user_data(&arg);
		CHECK(f1.user_data() == f2.user_data());
	}

	SECTION("invoke")
	{
		arg = 0;
		f1();
		CHECK(arg == 1);
		b();
		CHECK(arg == 2);
	}

	SECTION("task equals")
	{
		CHECK(f1 == f2);
		CHECK_FALSE(f1 == b);
	}

	SECTION("task not equals")
	{
		CHECK_FALSE(f1 != f2);
		CHECK(f1 != b);
	}

	SECTION("function_ptr equals")
	{
		CHECK(foo == f1);
		CHECK(f1 == foo);
		CHECK_FALSE(bar == f1);
		CHECK_FALSE(f1 == bar);
	}

	SECTION("function_ptr not equals")
	{
		CHECK_FALSE(foo != f1);
		CHECK_FALSE(f1 != foo);
		CHECK(bar != f1);
		CHECK(f1 != bar);
	}
}


} // namespace
