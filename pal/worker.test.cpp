#include <pal/worker>
#include <pal/test>


namespace {


void inc_1 (void *arg) noexcept
{
	auto i = static_cast<int *>(arg);
	(*i)++;
}


void inc_2 (void *arg) noexcept
{
	auto i = static_cast<int *>(arg);
	*i += 2;
}


TEST_CASE("worker")
{
	int arg = 0;
	pal::task i1{inc_1, &arg}, i2{inc_2, &arg};

	pal::task::mpsc_queue completed;
	CHECK(completed.try_pop() == nullptr);

	pal::worker worker(completed);

	SECTION("post")
	{
		arg = 0;
		worker.post(&i1);
		CHECK(completed.try_pop() == &i1);
		CHECK(arg == 0);
	}

	SECTION("submit")
	{
		arg = 0;
		worker.submit(&i1);
		CHECK(completed.try_pop() == &i1);
		CHECK(arg == 1);
	}

	SECTION("multiple post")
	{
		arg = 2;
		worker.post(&i1);
		worker.post(&i2);
		CHECK(arg == 2);
		CHECK(completed.try_pop() == &i1);
		CHECK(completed.try_pop() == &i2);
		CHECK(arg == 2);
	}

	SECTION("multiple submit")
	{
		arg = 2;
		worker.submit(&i1);
		worker.submit(&i2);
		CHECK(arg == 5);
		CHECK(completed.try_pop() == &i1);
		CHECK(completed.try_pop() == &i2);
		CHECK(arg == 5);
	}

	CHECK(completed.try_pop() == nullptr);
}


} // namespace
