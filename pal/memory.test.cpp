#include <pal/memory.hpp>
#include <pal/test.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>

namespace
{

TEST_CASE("temporary_buffer")
{
	SECTION("inline")
	{
		pal::temporary_buffer<2> buf{1};
		REQUIRE(buf);
		CHECK(buf.get().data() != nullptr);
		CHECK(buf.get().size() == 1);
		CHECK(buf.is_inline());
	}

	SECTION("inline nothrow")
	{
		pal::temporary_buffer<2> buf{std::nothrow, 1};
		REQUIRE(buf);
		CHECK(buf.get().data() != nullptr);
		CHECK(buf.get().size() == 1);
		CHECK(buf.is_inline());
	}

	SECTION("heap")
	{
		pal::temporary_buffer<1> buf{2};
		REQUIRE(buf);
		CHECK(buf.get().data() != nullptr);
		CHECK(buf.get().size() == 2);
		CHECK_FALSE(buf.is_inline());
	}

	SECTION("heap nothrow")
	{
		pal::temporary_buffer<1> buf{std::nothrow, 2};
		REQUIRE(buf);
		CHECK(buf.get().data() != nullptr);
		CHECK(buf.get().size() == 2);
		CHECK_FALSE(buf.is_inline());
	}

	SECTION("heap failure")
	{
		auto f = [] ()
		{
			const pal_test::bad_alloc_once x;
			pal::temporary_buffer<1> buf{2};
			CAPTURE(buf.get().data());
			FAIL();
		};
		CHECK_THROWS_AS(f(), std::bad_alloc);
	}

	SECTION("heap nothrow failure")
	{
		const pal_test::bad_alloc_once x;
		pal::temporary_buffer<1> buf{std::nothrow, 2};
		CHECK_FALSE(buf);
		CHECK(buf.get().empty());
		CHECK_FALSE(buf.is_inline());
	}
}

TEST_CASE("alloc_result - void")
{
	static constexpr auto throw_bad_alloc = [] (bool do_throw)
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

struct raw_ptr
{
	static constexpr bool auto_release = false;

	static auto alloc (int arg)
	{
		return pal::alloc_result([&] { return new int(arg); });
	}
};

struct nothrow_raw_ptr
{
	static constexpr bool auto_release = false;

	static auto alloc (int arg)
	{
		return pal::alloc_result([&] { return new (std::nothrow) int(arg); });
	}
};

struct unique_ptr
{
	static constexpr bool auto_release = true;

	static auto alloc (int arg)
	{
		return pal::make_unique<int>(arg);
	}
};

struct shared_ptr
{
	static constexpr bool auto_release = true;

	static auto alloc (int arg)
	{
		return pal::make_shared<int>(arg);
	}
};

TEMPLATE_TEST_CASE("alloc_result", "", raw_ptr, nothrow_raw_ptr, unique_ptr, shared_ptr)
{
	SECTION("success")
	{
		auto p = TestType::alloc(1);
		REQUIRE(p.has_value());
		CHECK(*p.value() == 1);

		if constexpr (!TestType::auto_release)
		{
			delete p.value();
		}
	}

	SECTION("failure")
	{
		const pal_test::bad_alloc_once x;
		auto p = TestType::alloc(2);
		REQUIRE_FALSE(p.has_value());
		CHECK(p.error() == std::errc::not_enough_memory);
	}
}

} // namespace
