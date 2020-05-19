#include <pal/task>
#include <pal/test>
#include <chrono>
#include <thread>


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


TEST_CASE("task::completion_queue")
{
	using namespace std::chrono_literals;
	constexpr auto moment = 5ms;
	constexpr auto forever = 1min;
	const auto now = std::chrono::system_clock::now();
	const auto abs_moment = now + moment;
	const auto abs_forever = now + forever;

	int arg = 0;
	pal::task task{foo, &arg};
	pal::task::completion_queue queue;
	CHECK(queue.try_get() == nullptr);

	SECTION("post / try_get")
	{
		queue.post(task);
		CHECK(queue.try_get() == &task);
	}

	SECTION("wait / post")
	{
		queue.post(task);
		CHECK(queue.wait() == &task);
	}

	SECTION("wait / deferred post")
	{
		auto thread = std::thread([&]
		{
			std::this_thread::sleep_for(moment);
			queue.post(task);
		});
		CHECK(queue.wait() == &task);
		thread.join();
	}

	SECTION("wait_until / timeout")
	{
		CHECK(queue.wait_until(abs_moment) == nullptr);
	}

	SECTION("wait_until")
	{
		queue.post(task);
		CHECK(queue.wait_until(abs_moment) == &task);
	}

	SECTION("wait_until / deferred post")
	{
		auto thread = std::thread([&]
		{
			std::this_thread::sleep_for(moment);
			queue.post(task);
		});
		CHECK(queue.wait_until(abs_forever) == &task);
		thread.join();
	}

	SECTION("wait_for / timeout")
	{
		CHECK(queue.wait_for(moment) == nullptr);
	}

	SECTION("wait_for")
	{
		queue.post(task);
		CHECK(queue.wait_for(moment) == &task);
	}

	SECTION("wait_for / deferred post")
	{
		auto thread = std::thread([&]
		{
			std::this_thread::sleep_for(moment);
			queue.post(task);
		});
		CHECK(queue.wait_for(forever) == &task);
		thread.join();
	}

	CHECK(arg == 0);
}


} // namespace
