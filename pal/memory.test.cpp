#include <pal/memory>
#include <pal/test>
#include <catch2/catch_template_test_macros.hpp>
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

struct raw_ptr
{
	static auto alloc (int arg)
	{
		return pal::alloc_result([&]{ return new int(arg); });
	}
};

struct nothrow_raw_ptr
{
	static auto alloc (int arg)
	{
		return pal::alloc_result([&]{ return new(std::nothrow) int(arg); });
	}
};

struct unique_ptr
{
	static auto alloc (int arg)
	{
		return pal::make_unique<int>(arg);
	}
};

struct shared_ptr
{
	static auto alloc (int arg)
	{
		return pal::make_shared<int>(arg);
	}
};

TEST_CASE("alloc - void")
{
	static constexpr auto throw_bad_alloc = [](bool do_throw)
	{
		if (do_throw)
		{
			throw std::bad_alloc();
		}
	};

	SECTION("success")
	{
		auto x = pal::alloc_result(throw_bad_alloc, false);
		REQUIRE(x);
	}

	SECTION("failure")
	{
		auto x = pal::alloc_result(throw_bad_alloc, true);
		REQUIRE_FALSE(x);
		CHECK(x.error() == std::errc::not_enough_memory);
	}
}

TEMPLATE_TEST_CASE("alloc", "",
	raw_ptr,
	nothrow_raw_ptr,
	unique_ptr,
	shared_ptr)
{
	static constexpr bool auto_release = false
		|| std::is_same_v<TestType, unique_ptr>
		|| std::is_same_v<TestType, shared_ptr>
	;

	SECTION("success")
	{
		auto p = TestType::alloc(1);
		REQUIRE(p.has_value());
		CHECK(*p.value() == 1);

		if constexpr (!auto_release)
		{
			delete p.value();
		}
	}

	SECTION("failure")
	{
		pal_test::bad_alloc_once x;
		auto p = TestType::alloc(2);
		REQUIRE_FALSE(p.has_value());
		CHECK(p.error() == std::errc::not_enough_memory);
	}
}

} // namespace
