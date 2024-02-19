#include <pal/memory>
#include <pal/test>
#include <catch2/catch_test_macros.hpp>

namespace {

TEST_CASE("temporary_buffer")
{
	SECTION("stack")
	{
		pal::temporary_buffer<2> ptr(1);
		CHECK(ptr.get() != nullptr);
		CHECK(ptr.get() == ptr.stack());
		CHECK(ptr);
	}

	SECTION("stack std::nothrow")
	{
		pal::temporary_buffer<2> ptr(std::nothrow, 1);
		CHECK(ptr.get() != nullptr);
		CHECK(ptr.get() == ptr.stack());
		CHECK(ptr);
	}

	SECTION("heap")
	{
		pal::temporary_buffer<1> ptr(2);
		CHECK(ptr.get() != nullptr);
		CHECK(ptr.get() != ptr.stack());
		CHECK(ptr);
	}

	SECTION("heap std::nothrow")
	{
		pal::temporary_buffer<1> ptr(std::nothrow, 2);
		CHECK(ptr.get() != nullptr);
		CHECK(ptr.get() != ptr.stack());
		CHECK(ptr);
	}

	SECTION("heap failure")
	{
		auto f = []()
		{
			pal_test::bad_alloc_once x;
			pal::temporary_buffer<1> ptr(2);
			CAPTURE(ptr.get());
			FAIL();
		};
		CHECK_THROWS_AS(f(), std::bad_alloc);
	}

	SECTION("heap std::nothrow failure")
	{
		pal_test::bad_alloc_once x;
		pal::temporary_buffer<1> ptr(std::nothrow, 2);
		CHECK(ptr.get() == nullptr);
		CHECK(ptr.get() != ptr.stack());
	}
}

} // namespace
