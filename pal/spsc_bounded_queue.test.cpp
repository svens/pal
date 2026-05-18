#include <pal/spsc_bounded_queue.hpp>
#include <pal/test.hpp>
#include <array>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <thread>

namespace
{

TEST_CASE("spsc_bounded_queue")
{
	struct foo
	{
		size_t seq = 0;
		int delivery_count = 0;
	};

	pal::spsc_bounded_queue<foo, 4> queue;
	CHECK(queue.empty());
	CHECK_FALSE(queue.full());
	CHECK(queue.try_pop() == nullptr);

	SECTION("push/try_pop: single")
	{
		foo f;
		CHECK(queue.push(f));
		CHECK_FALSE(queue.empty());
		CHECK(queue.try_pop() == &f);
	}

	SECTION("push/try_pop: fifo order")
	{
		foo f1, f2, f3;
		CHECK(queue.push(f1));
		CHECK(queue.push(f2));
		CHECK(queue.push(f3));

		CHECK(queue.try_pop() == &f1);
		CHECK(queue.try_pop() == &f2);
		CHECK(queue.try_pop() == &f3);
		CHECK(queue.try_pop() == nullptr);
	}

	SECTION("push/try_pop: interleaved")
	{
		foo f1, f2, f3;
		CHECK(queue.push(f1));
		CHECK(queue.push(f2));
		CHECK(queue.try_pop() == &f1);
		CHECK(queue.push(f3));
		CHECK(queue.try_pop() == &f2);
		CHECK(queue.try_pop() == &f3);
	}

	SECTION("full")
	{
		foo f1, f2, f3, f4, f5;
		CHECK(queue.push(f1));
		CHECK(queue.push(f2));
		CHECK(queue.push(f3));
		CHECK(queue.push(f4));
		CHECK(queue.full());
		CHECK_FALSE(queue.push(f5));

		CHECK(queue.try_pop() == &f1);
		CHECK_FALSE(queue.full());
		CHECK(queue.push(f5));

		CHECK(queue.try_pop() == &f2);
		CHECK(queue.try_pop() == &f3);
		CHECK(queue.try_pop() == &f4);
		CHECK(queue.try_pop() == &f5);
	}

	SECTION("threaded")
	{
		std::array<foo, 10'000> data;
		for (size_t i = 0; i < data.size(); ++i)
		{
			data[i].seq = i + 1;
		}

		pal::spsc_bounded_queue<foo, 16> q;
		std::atomic<bool> producer_done{};
		bool ordering_ok = true;

		// clang-format off

		auto consumer = std::thread([&]
		{
			size_t last_seq = 0;
			while (!producer_done || !q.empty())
			{
				if (auto *p = q.try_pop())
				{
					p->delivery_count++;
					if (p->seq != ++last_seq)
					{
						ordering_ok = false;
					}
				}
				else
				{
					std::this_thread::yield();
				}
			}
		});

		using namespace std::chrono_literals;
		std::this_thread::sleep_for(1ms);

		for (auto &f: data)
		{
			while (!q.push(f))
			{
				std::this_thread::yield();
			}
		}
		producer_done = true;

		// clang-format on

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
	CHECK_FALSE(queue.full());
	CHECK(queue.try_pop() == nullptr);
}

} // namespace
