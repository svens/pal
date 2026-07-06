#include <pal/async/event_loop.hpp>
#include <pal/version.hpp>

#if !__pal_os_windows

#include <pal/async/task.hpp>
#include <pal/test.hpp>
#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <chrono>
#include <thread>
#include <tuple>

namespace
{

using namespace pal::async;
using namespace std::chrono_literals;

// No NSDMIs: the completion machinery memcpys single-shot closures, and g++'s -Wclass-memaccess rejects that
// for any type with a non-trivial default ctor. Designated-init zero-fills the omitted members.
struct record
{
	int *ran;
	task **got;

	void operator() (task_ptr &&p) const noexcept
	{
		++*ran;
		if (got != nullptr)
		{
			*got = p.get();
		}
	}
};

struct bump
{
	std::atomic<int> *ran;

	void operator() (task_ptr &&) const noexcept
	{
		ran->fetch_add(1, std::memory_order_relaxed);
	}
};

TEST_CASE("async/event_loop")
{
	auto loop = make_loop();
	REQUIRE(loop);

	SECTION("run: idle")
	{
		CHECK(loop->stats().completions == 0);
		CHECK(loop->stats().wakeups == 0);

		// Idle run() has nothing to do and returns immediately.
		auto n = loop->run();
		REQUIRE(n);
		CHECK(*n == 0);
	}

	SECTION("run_for: idle")
	{
		auto n = loop->run_for(5ms);
		REQUIRE(n);
		CHECK(*n == 0);
	}

	SECTION("run_once: idle")
	{
		auto n = loop->run_once();
		REQUIRE(n);
		CHECK(*n == 0);
	}

	SECTION("now")
	{
		const auto before = loop->now();
		std::ignore = loop->run_for(2ms);
		CHECK(loop->now() >= before);
	}

	SECTION("post")
	{
		task t;
		int ran = 0;
		task *got = nullptr;
		loop->post(t.borrow(), record{.ran = &ran, .got = &got});

		auto n = loop->run_once();
		REQUIRE(n);
		CHECK(*n == 1);
		CHECK(ran == 1);
		CHECK(got == &t);
		CHECK(loop->stats().completions == 1);
	}

	SECTION("post: multiple")
	{
		task a, b, c;
		int ran = 0;
		loop->post(a.borrow(), record{.ran = &ran, .got = nullptr});
		loop->post(b.borrow(), record{.ran = &ran, .got = nullptr});
		loop->post(c.borrow(), record{.ran = &ran, .got = nullptr});

		auto n = loop->run();
		REQUIRE(n);
		CHECK(*n == 3);
		CHECK(ran == 3);
	}

	SECTION("post: cross-thread")
	{
		task t;
		std::atomic<int> ran = 0;

		// clang-format off
		std::thread producer{[&]
		{
			std::this_thread::sleep_for(20ms);
			loop->post(t.borrow(), bump{.ran = &ran});
		}};
		// clang-format on

		// run_for() blocks in poll() until the producer thread's wake() unblocks it, well before the timeout.
		auto n = loop->run_for(5s);
		producer.join();

		REQUIRE(n);
		CHECK(*n == 1);
		CHECK(ran.load() == 1);
		CHECK(loop->stats().wakeups >= 1);
	}
}

TEST_CASE("async/event_loop destructor contract")
{
	if constexpr (pal::build == pal::build_type::debug)
	{
		// A loop destroyed with an undrained inbox means a task is still live -> REQUIRE crash.
		static task t;

		// clang-format off
		auto msg = pal_test::require_terminate([]
		{
			auto loop = make_loop().value();
			loop.post(t.borrow(), [] (task_ptr &&) noexcept {});
		});
		// clang-format on

		CHECK(msg.contains("inbox"));
	}
}

} // namespace

#endif // !__pal_os_windows
