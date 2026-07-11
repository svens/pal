#include <pal/async/event_loop.hpp>
#include <pal/version.hpp>

#if !__pal_os_windows

#include <pal/async/task.hpp>
#include <pal/test.hpp>
#include <catch2/catch_test_macros.hpp>
#include <array>
#include <atomic>
#include <chrono>
#include <thread>
#include <tuple>

namespace
{

using namespace pal::async;
using namespace std::chrono_literals;

// Handlers are lambdas except where one must re-arm with itself (*this) -- a lambda cannot name itself.
struct periodic
{
	event_loop *loop;
	int *fired;

	void operator() (task_ptr &&t) const noexcept
	{
		if (++*fired < 3)
		{
			loop->post_after(std::move(t), 1ms, *this);
		}
	}
};

struct session
{
	event_loop::clock::time_point deadline;
	int expired;
};

// The lazy re-arm idiom (there is no cancel_timer): the session holds the authoritative deadline, refresh is
// a plain store, and a timer that fires early re-arms itself for the remainder.
struct lazy_rearm
{
	event_loop *loop;
	session *s;

	void operator() (task_ptr &&t) const noexcept
	{
		if (loop->now() < s->deadline)
		{
			loop->post_after(std::move(t), s->deadline - loop->now(), *this);
			return;
		}
		++s->expired;
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

		// clang-format off
		loop->post(t.borrow(), [&](task_ptr &&p) noexcept
		{
			++ran;
			got = p.get();
		});
		// clang-format on

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
		const auto bump = [&ran] (task_ptr &&) noexcept
		{
			++ran;
		};
		loop->post(a.borrow(), bump);
		loop->post(b.borrow(), bump);
		loop->post(c.borrow(), bump);

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
			loop->post(t.borrow(), [&ran] (task_ptr &&) noexcept
			{
				ran.fetch_add(1, std::memory_order_relaxed);
			});
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

	SECTION("post_after: immediate")
	{
		task t;
		int fired = 0;
		task *got = nullptr;
		// clang-format off
		loop->post_after(t.borrow(), 0ms, [&](task_ptr &&p) noexcept
		{
			++fired;
			got = p.get();
		});
		// clang-format on

		auto n = loop->run_once();
		REQUIRE(n);
		CHECK(*n == 1);
		CHECK(fired == 1);
		CHECK(got == &t);
		CHECK(loop->stats().completions == 1);
	}

	SECTION("post_after: expiry ordering")
	{
		task a, b, c;
		std::array<int, 4> order{};
		size_t count = 0;

		loop->post_after(a.borrow(), 30ms, [&] (task_ptr &&) noexcept { order[count++] = 1; });
		loop->post_after(b.borrow(), 10ms, [&] (task_ptr &&) noexcept { order[count++] = 2; });
		loop->post_after(c.borrow(), 20ms, [&] (task_ptr &&) noexcept { order[count++] = 3; });

		auto n = loop->run();
		REQUIRE(n);
		CHECK(*n == 3);
		REQUIRE(count == 3);
		CHECK(order[0] == 2);
		CHECK(order[1] == 3);
		CHECK(order[2] == 1);
	}

	SECTION("post_after: many timers expire in deadline order")
	{
		// Insert earliest-deadline-first so the root accumulates every other timer as a child; popping that
		// fat-child root drives the pairing heap's two-pass merge through its second-pass meld (root list > 1).
		std::array<task, 6> timers;
		std::array<size_t, 6> order{};
		size_t count = 0;

		for (size_t i = 0; i < timers.size(); ++i)
		{
			const auto delay = std::chrono::milliseconds(5 * (i + 1));
			loop->post_after(
				timers[i].borrow(),
				delay,
				[&order, &count, i] (task_ptr &&) noexcept { order[count++] = i; }
			);
		}

		auto n = loop->run();
		REQUIRE(n);
		CHECK(*n == timers.size());
		REQUIRE(count == timers.size());
		for (size_t i = 0; i < order.size(); ++i)
		{
			CHECK(order[i] == i);
		}
	}

	SECTION("run_for: negative timeout polls without blocking")
	{
		// A negative timeout clamped to zero; run_for returns immediately like a run_once.
		auto n = loop->run_for(-5ms);
		REQUIRE(n);
		CHECK(*n == 0);
	}

	SECTION("post: cross-thread wakes an unbounded run_for")
	{
		// duration::max() blocks; only the producer's cross-thread wake() can unblock it.
		task t;
		std::atomic<int> ran = 0;

		// clang-format off
		std::thread producer{[&]
		{
			std::this_thread::sleep_for(20ms);
			loop->post(t.borrow(), [&ran] (task_ptr &&) noexcept
			{
				ran.fetch_add(1, std::memory_order_relaxed);
			});
		}};
		// clang-format on

		auto n = loop->run_for(event_loop::clock::duration::max());
		producer.join();

		REQUIRE(n);
		CHECK(*n == 1);
		CHECK(ran.load() == 1);
		CHECK(loop->stats().wakeups >= 1);
	}

	SECTION("post_after: caps run_for poll timeout")
	{
		task t;
		int fired = 0;

		const auto before = loop->now();
		loop->post_after(t.borrow(), 10ms, [&fired] (task_ptr &&) noexcept { ++fired; });

		// run_for() returns at expiry, well before the timeout, because poll() is capped to the next deadline.
		auto n = loop->run_for(5s);
		REQUIRE(n);
		CHECK(*n == 1);
		CHECK(fired == 1);
		CHECK(loop->now() - before < 5s);
	}

	SECTION("now: cached between iterations")
	{
		const auto cached = loop->now();
		std::this_thread::sleep_for(2ms);
		CHECK(loop->now() == cached);

		std::ignore = loop->run_once();
		CHECK(loop->now() > cached);
	}

	SECTION("post_after: rebind inside own callback")
	{
		task t;
		int fired = 0;
		loop->post_after(t.borrow(), 1ms, periodic{.loop = &*loop, .fired = &fired});

		auto n = loop->run();
		REQUIRE(n);
		CHECK(*n == 3);
		CHECK(fired == 3);
	}

	SECTION("post_after: lazy re-arm")
	{
		task t;
		const auto base = loop->now();
		session s{.deadline = base + 5ms, .expired = 0};
		loop->post_after(t.borrow(), 5ms, lazy_rearm{.loop = &*loop, .s = &s});

		// refresh: move the authoritative deadline without touching the armed timer
		s.deadline = base + 40ms;

		auto n = loop->run();
		REQUIRE(n);
		CHECK(*n == 2);
		CHECK(s.expired == 1);
		CHECK(loop->now() >= base + 40ms);
	}

	SECTION("run: post and post_after both pending")
	{
		task a, b;
		int posted = 0, fired = 0;
		loop->post(a.borrow(), [&posted] (task_ptr &&) noexcept { ++posted; });
		loop->post_after(b.borrow(), 1ms, [&fired] (task_ptr &&) noexcept { ++fired; });

		auto n = loop->run();
		REQUIRE(n);
		CHECK(*n == 2);
		CHECK(posted == 1);
		CHECK(fired == 1);
	}

	SECTION("post_after: pending at loop destruction")
	{
		task t;
		int fired = 0;
		{
			auto inner = make_loop();
			REQUIRE(inner);
			inner->post_after(t.borrow(), 1h, [&fired] (task_ptr &&) noexcept { ++fired; });
		}
		// dropped without completing; no destructor REQUIRE trips, the handler never ran
		CHECK(fired == 0);
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
