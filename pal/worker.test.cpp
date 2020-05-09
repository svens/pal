#include <pal/worker>
#include <pal/test>


namespace {


void inc_1 (pal::task &arg) noexcept
{
	auto i = static_cast<int *>(arg.user_data());
	*i += 1;
}


void inc_2 (pal::task &arg) noexcept
{
	auto i = static_cast<int *>(arg.user_data());
	*i += 2;
}


TEST_CASE("worker")
{
	int arg = 0;
	pal::task i1{&inc_1, &arg}, i2{&inc_2, &arg};
	pal::worker worker;

	SECTION("submit")
	{
		arg = 0;
		worker.submit(&i1);
		CHECK(arg == 1);
	}

	SECTION("multiple submit")
	{
		arg = 2;
		worker.submit(&i1);
		CHECK(arg == 3);
		worker.submit(&i2);
		CHECK(arg == 5);
	}

	SECTION("submit null")
	{
		if constexpr (!pal::expect_noexcept)
		{
			pal::task *t = nullptr;
			CHECK_THROWS_AS(
				worker.submit(t),
				std::logic_error
			);
		}
	}
}


} // namespace
