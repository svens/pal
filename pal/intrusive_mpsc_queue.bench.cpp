#include <pal/intrusive_mpsc_queue.hpp>
#include <pal/intrusive_queue.hpp>
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

struct mpsc_node
{
	pal::intrusive_mpsc_queue_hook<mpsc_node> hook;
	std::atomic<int> state{0}; // 0=available, -1=in-use, 1=waiting
	using queue = pal::intrusive_mpsc_queue<&mpsc_node::hook>;
};

struct sq_node
{
	pal::intrusive_queue_hook<sq_node> hook;
	std::atomic<int> state{0}; // 0=available, -1=in-use, 1=waiting
	using queue = pal::intrusive_queue<&sq_node::hook>;
};

struct locked_queue
{
	std::mutex mutex;
	sq_node::queue queue;

	void push (sq_node &n)
	{
		const std::scoped_lock lock{mutex};
		queue.push(n);
	}

	sq_node *try_pop ()
	{
		const std::scoped_lock lock{mutex};
		return queue.try_pop();
	}
};

const auto run_bench = [] (auto meter, size_t n_producers, size_t M, auto &pool, auto &queue)
{
	const size_t K = pool.size() / n_producers;
	const size_t total = n_producers * M;

	std::atomic<bool> running{true};
	std::barrier<> start_barrier{static_cast<std::ptrdiff_t>(n_producers + 2)};
	std::barrier<> end_barrier{static_cast<std::ptrdiff_t>(n_producers + 2)};

	// clang-format off

	std::vector<std::thread> producers(n_producers);
	for (size_t p = 0; p < n_producers; ++p)
	{
		producers[p] = std::thread([&, p]
		{
			const size_t base = p * K;
			while (true)
			{
				start_barrier.arrive_and_wait();
				if (!running)
				{
					break;
				}

				for (size_t push = 0; push < M; ++push)
				{
					auto &item = pool[base + (push % K)];
					int expected = -1;
					if (item.state.compare_exchange_strong(expected, 1,
						std::memory_order_acquire,
						std::memory_order_acquire))
					{
						item.state.wait(1, std::memory_order_acquire);
					}
					item.state.store(-1, std::memory_order_relaxed);
					queue.push(item);
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

			size_t count = 0;
			while (count < total)
			{
				if (auto *n = queue.try_pop())
				{
					const int old = n->state.exchange(0, std::memory_order_release);
					if (old > 0)
					{
						n->state.notify_one();
					}
					++count;
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
	for (auto &t: producers)
	{
		t.join();
	}
	consumer.join();
};

TEST_CASE("intrusive_mpsc_queue", "[!benchmark]")
{
	const auto n_producers = static_cast<size_t>(GENERATE(1, 2, 4, 8));
	const size_t K = 16;
	const size_t M = 10'000;

	BENCHMARK_ADVANCED("mpsc/" + std::to_string(n_producers))(auto meter)
	{
		mpsc_node::queue queue;
		std::vector<mpsc_node> pool(n_producers * K);
		run_bench(meter, n_producers, M, pool, queue);
	};

	BENCHMARK_ADVANCED("locked/" + std::to_string(n_producers))(auto meter)
	{
		locked_queue queue;
		std::vector<sq_node> pool(n_producers * K);
		run_bench(meter, n_producers, M, pool, queue);
	};
}

} // namespace
