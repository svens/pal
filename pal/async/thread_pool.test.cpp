#include <pal/async/thread_pool.hpp>
#include <pal/test.hpp>
#include <pal/version.hpp>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <tuple>

namespace
{

using namespace pal::async;
using namespace std::chrono_literals;

TEST_CASE("async/thread_pool")
{
	SECTION("make_thread_pool: create and destroy idle")
	{
		auto pool = make_thread_pool(2);
		REQUIRE(pool);
	}
}

TEST_CASE("async/thread_pool make contract")
{
	if constexpr (pal::build == pal::build_type::debug)
	{
		// clang-format off
		auto msg = pal_test::require_terminate([]
		{
			std::ignore = make_thread_pool(0);
		});
		// clang-format on

		CHECK(msg.contains("worker threads"));
	}
}

#if !__pal_os_windows

// Handlers are lambdas except where one must re-post with itself (*this) -- a lambda cannot name itself.
struct round_trip
{
	thread_pool *pool;
	event_loop *loop;
	int *rounds;

	void operator() (task_ptr &&t) const noexcept
	{
		if (++*rounds < 3)
		{
			pool->post(*loop, std::move(t), [] (task &) noexcept {}, *this);
		}
	}
};

// run_for() until \a total completions arrive; a zero-completion iteration means run_for() hit its full
// timeout, so fail instead of spinning forever.
void run_until (event_loop &loop, size_t total)
{
	size_t n = 0;
	while (n < total)
	{
		auto r = loop.run_for(5s);
		REQUIRE(r);
		REQUIRE(*r > 0);
		n += *r;
	}
	CHECK(n == total);
}

TEST_CASE("async/thread_pool offload")
{
	auto loop = make_loop();
	REQUIRE(loop);

	auto pool = make_thread_pool(1);
	REQUIRE(pool);

	SECTION("post: work on worker thread, handler on loop thread")
	{
		task t;
		std::thread::id work_thread{};
		std::thread::id handler_thread{};
		task *got = nullptr;

		// clang-format off
		pool->post(*loop, t.borrow(),
			[&] (task &) noexcept
			{
				work_thread = std::this_thread::get_id();
			},
			[&] (task_ptr &&p) noexcept
			{
				handler_thread = std::this_thread::get_id();
				got = p.get();
			}
		);
		// clang-format on

		auto n = loop->run_for(5s);
		REQUIRE(n);
		CHECK(*n == 1);
		CHECK(work_thread != std::this_thread::get_id());
		CHECK(handler_thread == std::this_thread::get_id());
		CHECK(got == &t);
		CHECK(loop->stats().completions == 1);
	}

	SECTION("post: result through scratch")
	{
		struct answer
		{
			int value;
		};

		task t;
		int seen = 0;

		// clang-format off
		pool->post(*loop, t.borrow(),
			[] (task &w) noexcept
			{
				w.scratch_as<answer>().value = 42;
			},
			[&] (task_ptr &&p) noexcept
			{
				seen = p->scratch_as<answer>().value;
			}
		);
		// clang-format on

		run_until(*loop, 1);
		CHECK(seen == 42);
	}

	SECTION("post: work may overwrite the entire scratch")
	{
		task t;
		bool intact = false;

		// clang-format off
		pool->post(*loop, t.borrow(),
			[] (task &w) noexcept
			{
				std::ranges::fill(w.scratch(), std::byte{0xa5});
			},
			[&] (task_ptr &&p) noexcept
			{
				intact = std::ranges::all_of(p->scratch(), [] (std::byte b)
				{
					return b == std::byte{0xa5};
				});
			}
		);
		// clang-format on

		run_until(*loop, 1);
		CHECK(intact);
	}

	SECTION("post: post-back wakes a blocked poll")
	{
		task t;
		int ran = 0;

		// clang-format off
		pool->post(*loop, t.borrow(),
			[] (task &) noexcept
			{
				std::this_thread::sleep_for(20ms);
			},
			[&] (task_ptr &&) noexcept
			{
				++ran;
			}
		);
		// clang-format on

		// run_for() blocks in poll() until the worker's post-back wake() unblocks it, well before the timeout
		const auto before = event_loop::clock::now();
		auto n = loop->run_for(5s);
		REQUIRE(n);
		CHECK(*n == 1);
		CHECK(ran == 1);
		CHECK(event_loop::clock::now() - before < 5s);
		CHECK(loop->stats().wakeups >= 1);
	}

	SECTION("post: multiple tasks")
	{
		task a, b, c;
		int ran = 0;
		const auto work = [] (task &) noexcept {};
		const auto done = [&ran] (task_ptr &&) noexcept
		{
			++ran;
		};

		pool->post(*loop, a.borrow(), work, done);
		pool->post(*loop, b.borrow(), work, done);
		pool->post(*loop, c.borrow(), work, done);

		run_until(*loop, 3);
		CHECK(ran == 3);
	}

	SECTION("post: multiple loops sharing one pool")
	{
		auto other = make_loop();
		REQUIRE(other);

		task a, b;
		int fired = 0, other_fired = 0;
		const auto work = [] (task &) noexcept {};

		pool->post(*loop, a.borrow(), work, [&fired] (task_ptr &&) noexcept { ++fired; });
		pool->post(*other, b.borrow(), work, [&other_fired] (task_ptr &&) noexcept { ++other_fired; });

		run_until(*loop, 1);
		CHECK(fired == 1);
		CHECK(other_fired == 0);

		run_until(*other, 1);
		CHECK(other_fired == 1);
	}

	SECTION("post: task reuse round trips")
	{
		task t;
		int rounds = 0;

		// clang-format off
		pool->post(*loop, t.borrow(),
			[] (task &) noexcept {},
			round_trip{.pool = &*pool, .loop = &*loop, .rounds = &rounds}
		);
		// clang-format on

		run_until(*loop, 3);
		CHECK(rounds == 3);
	}
}

TEST_CASE("async/thread_pool destructor contract")
{
	if constexpr (pal::build == pal::build_type::debug)
	{
		// A pool destroyed with a queued task means the app did not quiesce -> REQUIRE crash. The single
		// worker holds at most `first`; `second` stays queued behind it until `release`.
		static task first, second;
		static std::atomic<bool> release = false;

		// clang-format off
		auto msg = pal_test::require_terminate([]
		{
			auto loop = make_loop().value();
			auto pool = make_thread_pool(1).value();
			const auto work = [] (task &) noexcept
			{
				while (!release)
				{
					std::this_thread::yield();
				}
			};
			const auto done = [] (task_ptr &&) noexcept {};
			pool.post(loop, first.borrow(), work, done);
			pool.post(loop, second.borrow(), work, done);
		});
		// clang-format on

		// unblock the leaked worker (the trapped terminate leaked the pool without joining it)
		release = true;
		CHECK(msg.contains("pending"));
	}
}

#endif // !__pal_os_windows

} // namespace
