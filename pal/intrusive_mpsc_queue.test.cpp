#include <pal/intrusive_mpsc_queue.hpp>
#include <pal/test.hpp>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <atomic>
#include <random>
#include <thread>
#include <vector>

namespace
{

TEST_CASE("intrusive_mpsc_queue")
{
	struct foo
	{
		pal::intrusive_mpsc_queue_hook<foo> hook;
		using queue = pal::intrusive_mpsc_queue<&foo::hook>;

		size_t producer_id = 0;
		size_t seq = 0;
		int delivery_count = 0;
	};
	static_assert(std::is_same_v<foo, foo::queue::value_type>);

	foo::queue queue{};
	CHECK(queue.empty());
	CHECK(queue.try_pop() == nullptr);

	SECTION("push/try_pop: single")
	{
		foo f;
		queue.push(f);
		REQUIRE_FALSE(queue.empty());
		CHECK(queue.try_pop() == &f);
	}

	SECTION("push/try_pop: multiple")
	{
		foo f1, f2, f3;
		queue.push(f1);
		queue.push(f2);
		queue.push(f3);

		REQUIRE_FALSE(queue.empty());
		CHECK(queue.try_pop() == &f1);
		REQUIRE_FALSE(queue.empty());
		CHECK(queue.try_pop() == &f2);
		REQUIRE_FALSE(queue.empty());
		CHECK(queue.try_pop() == &f3);
	}

	SECTION("push/try_pop: interleaved")
	{
		foo f1, f2, f3;
		queue.push(f1);
		queue.push(f2);

		REQUIRE_FALSE(queue.empty());
		CHECK(queue.try_pop() == &f1);

		queue.push(f3);

		REQUIRE_FALSE(queue.empty());
		CHECK(queue.try_pop() == &f2);
		queue.push(f2);

		REQUIRE_FALSE(queue.empty());
		CHECK(queue.try_pop() == &f3);

		REQUIRE_FALSE(queue.empty());
		CHECK(queue.try_pop() == &f2);
	}

	SECTION("threaded")
	{
		const size_t n_producers = std::clamp(std::thread::hardware_concurrency(), 2U, 16U);
		const size_t items_per_producer = 5'000;

		std::vector<foo> data(n_producers * items_per_producer);
		std::vector<std::thread> producers(n_producers);
		std::atomic<size_t> finished_producers{};
		bool ordering_ok = true;

		// clang-format off

		auto consumer = std::thread([&]
		{
			std::vector<size_t> last_seq(n_producers, 0);
			while (finished_producers != n_producers || !queue.empty())
			{
				if (auto *p = queue.try_pop())
				{
					p->delivery_count++;
					if (p->seq != ++last_seq[p->producer_id])
					{
						ordering_ok = false;
					}
				}
				std::this_thread::yield();
			}
		});

		using namespace std::chrono_literals;
		std::this_thread::sleep_for(1ms);

		const auto stagger = [](unsigned r)
		{
			if (r == 0)
			{
				std::this_thread::sleep_for(1ms);
			}
			else
			{
				std::this_thread::yield();
			}
		};

		for (size_t p = 0; p < n_producers; ++p)
		{
			producers[p] = std::thread([&, p]
			{
				std::minstd_rand rng(static_cast<unsigned>(p));
				const size_t base = p * items_per_producer;
				for (size_t s = 0; s < items_per_producer; ++s)
				{
					auto &f = data[base + s];
					f.producer_id = p;
					f.seq = s + 1;
					queue.push(f);
					stagger(rng() % 127);
				}
				finished_producers++;
			});
		}

		// clang-format on

		std::ranges::for_each(producers, [] (auto &t) { t.join(); });
		consumer.join();

		CHECK(ordering_ok);

		size_t wrong_deliveries = 0;
		for (const auto &f: data)
		{
			if (f.delivery_count != 1)
			{
				wrong_deliveries++;
			}
		}
		CHECK(wrong_deliveries == 0);
	}

	CHECK(queue.empty());
	CHECK(queue.try_pop() == nullptr);
}

} // namespace
