#include <pal/scoped_alloc>
#include <pal/test>


namespace {


template <size_t Size>
using alloc = pal::scoped_alloc<char, Size>;


TEST_CASE("scoped_alloc")
{
	auto as_const = [](const auto &ref) -> decltype(ref)
	{
		return ref;
	};

	SECTION("stack")
	{
		alloc<2> data(1);
		CHECK(data.get() != nullptr);
		CHECK(data.stack() == data.get());
		CHECK(as_const(data).get() != nullptr);
		CHECK(as_const(data).stack() == data.get());
		CHECK(data);
	}

	SECTION("stack std::nothrow")
	{
		alloc<2> data(std::nothrow, 1);
		CHECK(data.get() != nullptr);
		CHECK(data.stack() == data.get());
		CHECK(as_const(data).get() != nullptr);
		CHECK(as_const(data).stack() == data.get());
		CHECK(data);
	}

	SECTION("new")
	{
		alloc<1> data(2);
		CHECK(data.get() != nullptr);
		CHECK(data.stack() != data.get());
		CHECK(as_const(data).get() != nullptr);
		CHECK(as_const(data).stack() != data.get());
		CHECK(data);
	}

	SECTION("new std::nothrow")
	{
		alloc<1> data(std::nothrow, 2);
		CHECK(data.get() != nullptr);
		CHECK(data.stack() != data.get());
		CHECK(as_const(data).get() != nullptr);
		CHECK(as_const(data).stack() != data.get());
		CHECK(data);
	}

	SECTION("new failure")
	{
		auto f = []()
		{
			pal_test::bad_alloc_once x;
			alloc<1> data(2);

			// never reached, just create side-effect to disable
			// over-eager optimiser
			CHECK_FALSE(x.fail);
		};
		CHECK_THROWS_AS(f(), std::bad_alloc);
	}

	SECTION("new std::nothrow failure")
	{
		pal_test::bad_alloc_once x;
		alloc<1> data(std::nothrow, 2);
		CHECK(data.get() == nullptr);
		CHECK(data.stack() != nullptr);
		CHECK(as_const(data).get() == nullptr);
		CHECK(as_const(data).stack() != nullptr);
		CHECK_FALSE(data);
	}
}


} // namespace
