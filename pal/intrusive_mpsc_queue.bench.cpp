#include <pal/intrusive_mpsc_queue.hpp>
#include <pal/intrusive_queue.hpp>
#include <pal/intrusive_spsc_stack.hpp>
#include <pal/intrusive_stack.hpp>
#include <array>
#include <atomic>
#include <barrier>
#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <mutex>
#include <thread>
#include <vector>

namespace
{

struct lockfree
{
	struct task
	{
		pal::intrusive_mpsc_queue_hook<task> hook;
		lockfree *owner = nullptr;
	};

	using task_pool = pal::intrusive_spsc_stack<&task::hook>;
	task_pool pool;

	using task_queue = pal::intrusive_mpsc_queue<&task::hook>;

	task *alloc () noexcept
	{
		return pool.try_pop();
	}

	static void release (task &task) noexcept
	{
		task.owner->pool.push(task);
	}

	static void enqueue (task_queue &queue, task &task) noexcept
	{
		queue.push(task);
	}

	static task *dequeue (task_queue &queue) noexcept
	{
		return queue.try_pop();
	}
};

struct locked
{
	struct task
	{
		pal::intrusive_queue_hook<task> hook;
		locked *owner = nullptr;
	};

	using task_pool = pal::intrusive_stack<&task::hook>;
	task_pool pool;
	std::mutex pool_mutex;

	using task_queue = pal::intrusive_queue<&task::hook>;
	static inline std::mutex queue_mutex{};

	task *alloc () noexcept
	{
		const std::scoped_lock lock{pool_mutex};
		return pool.try_pop();
	}

	static void release (task &task) noexcept
	{
		const std::scoped_lock lock{task.owner->pool_mutex};
		task.owner->pool.push(task);
	}

	static void enqueue (task_queue &queue, task &task) noexcept
	{
		const std::scoped_lock lock{queue_mutex};
		queue.push(task);
	}

	static task *dequeue (task_queue &queue) noexcept
	{
		const std::scoped_lock lock{queue_mutex};
		return queue.try_pop();
	}
};

template <typename Impl>
struct producer
{
	Impl impl{};

	static constexpr size_t pool_depth = 64;
	std::array<typename Impl::task, pool_depth> tasks;

	std::thread thread;

	producer () noexcept
	{
		for (auto &task: tasks)
		{
			task.owner = &impl;
			impl.release(task);
		}
	}
};

template <typename Impl>
void run_bench (Catch::Benchmark::Chronometer &meter, size_t producer_count)
{
	typename Impl::task_queue queue;
	std::vector<producer<Impl>> producers{producer_count};

	constexpr size_t producer_rounds = 10'000;
	const size_t consumer_rounds = producer_count * producer_rounds;

	std::atomic<bool> running = true;
	std::barrier start_barrier{std::ssize(producers) + 2}, end_barrier{std::ssize(producers) + 2};

	// clang-format off

	for (auto &producer: producers)
	{
		producer.thread = std::thread([&]
		{
			while (true)
			{
				start_barrier.arrive_and_wait();
				if (!running)
				{
					break;
				}

				for (size_t round = 0; round < producer_rounds; /**/)
				{
					if (auto *task = producer.impl.alloc())
					{
						Impl::enqueue(queue, *task);
						round++;
					}
					else
					{
						std::this_thread::yield();
					}
				}

				end_barrier.arrive_and_wait();
			}
		});
	}

	auto consumer = std::thread([&]
	{
		while (true)
		{
			start_barrier.arrive_and_wait();
			if (!running)
			{
				break;
			}

			for (size_t round = 0; round < consumer_rounds; /**/)
			{
				if (auto *t = Impl::dequeue(queue))
				{
					Impl::release(*t);
					round++;
				}
				else
				{
					std::this_thread::yield();
				}
			}

			end_barrier.arrive_and_wait();
		}
	});

	meter.measure([&]
	{
		start_barrier.arrive_and_wait();
		end_barrier.arrive_and_wait();
	});

	// clang-format on

	running = false;
	start_barrier.arrive_and_wait();

	std::ranges::for_each(producers, [] (auto &producer) { producer.thread.join(); });
	consumer.join();
}

TEST_CASE("intrusive_mpsc_queue", "[!benchmark]")
{
	const size_t producer_count = GENERATE(1, 2, 4, 8);

	BENCHMARK_ADVANCED("lock-free/" + std::to_string(producer_count))(auto meter)
	{
		run_bench<lockfree>(meter, producer_count);
	};

	BENCHMARK_ADVANCED("locked/" + std::to_string(producer_count))(auto meter)
	{
		run_bench<locked>(meter, producer_count);
	};
}

} // namespace
