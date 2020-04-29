#include <pal/task>
#include <pal/test>


namespace {


// foo & bar implementations are not used but MSVC will optimize them into
// single function if no body


void foo (void *arg) noexcept
{
	*static_cast<int *>(arg) = 1;
}


void bar (void *arg) noexcept
{
	*static_cast<int *>(arg) = 2;
}


TEST_CASE("task")
{
	int arg;
	pal::task f1{foo, &arg}, f2{foo}, b{bar}, e;

	SECTION("operator bool")
	{
		CHECK(f1);
		CHECK(f2);
		CHECK(b);
		CHECK_FALSE(e);
	}

	SECTION("task equals")
	{
		CHECK(f1 == f2);
		CHECK_FALSE(f1 == b);
		CHECK_FALSE(f1 == e);
		CHECK_FALSE(b == e);
	}

	SECTION("task not equals")
	{
		CHECK_FALSE(f1 != f2);
		CHECK(f1 != b);
		CHECK(f1 != e);
		CHECK(b != e);
	}

	SECTION("function_ptr equals")
	{
		CHECK(foo == f1);
		CHECK(f1 == foo);
		CHECK_FALSE(bar == f1);
		CHECK_FALSE(f1 == bar);
		CHECK_FALSE(e == foo);
		CHECK_FALSE(e == bar);
	}

	SECTION("function_ptr not equals")
	{
		CHECK_FALSE(foo != f1);
		CHECK_FALSE(f1 != foo);
		CHECK(bar != f1);
		CHECK(f1 != bar);
		CHECK(e != foo);
		CHECK(e != bar);
	}

	SECTION("assign function_ptr")
	{
		pal::task e1;
		CHECK(e1 == e);
		CHECK(e1 != b);
		CHECK(e1 != f1);

		e1 = bar;
		CHECK(e1 != e);
		CHECK(e1 == b);
		CHECK(e1 != f1);

		e1 = foo;
		CHECK(e1 != e);
		CHECK(e1 != b);
		CHECK(e1 == f1);
	}

	SECTION("user_data")
	{
		CHECK(f1.user_data() == &arg);
		CHECK(f1 == f2);
		CHECK(f1.user_data() != f2.user_data());

		f2.user_data(&arg);
		CHECK(f1.user_data() == f2.user_data());
	}
}


} // namespace
