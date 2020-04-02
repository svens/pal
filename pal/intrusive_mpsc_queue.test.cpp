#include <pal/intrusive_mpsc_queue>
#include <pal/test>
#include <algorithm>
#include <array>
#include <thread>


namespace {


TEST_CASE("intrusive_mpsc_queue")
{
	struct foo
	{
		pal::intrusive_mpsc_queue_hook<foo> hook{};
		bool touched = false;
		using queue = pal::intrusive_mpsc_queue<&foo::hook>;
	};

	foo::queue queue{};
	CHECK(queue.empty());
	CHECK(queue.try_pop() == nullptr);


	SECTION("single_push_pop")
	{
		foo f;
		queue.push(&f);
		REQUIRE_FALSE(queue.empty());
		CHECK(queue.try_pop() == &f);
	}


	SECTION("multiple_push_pop")
	{
		foo f1, f2, f3;
		queue.push(&f1);
		queue.push(&f2);
		queue.push(&f3);

		REQUIRE_FALSE(queue.empty());
		CHECK(queue.try_pop() == &f1);

		REQUIRE_FALSE(queue.empty());
		CHECK(queue.try_pop() == &f2);

		REQUIRE_FALSE(queue.empty());
		CHECK(queue.try_pop() == &f3);
	}


	SECTION("interleaved_push_pop")
	{
		foo f1, f2, f3;
		queue.push(&f1);
		queue.push(&f2);

		// pop 1
		REQUIRE_FALSE(queue.empty());
		CHECK(queue.try_pop() == &f1);

		// push 3
		queue.push(&f3);

		// pop 2, push 2
		REQUIRE_FALSE(queue.empty());
		CHECK(queue.try_pop() == &f2);
		queue.push(&f2);

		// pop 3
		REQUIRE_FALSE(queue.empty());
		CHECK(queue.try_pop() == &f3);

		// pop 2
		REQUIRE_FALSE(queue.empty());
		CHECK(queue.try_pop() == &f2);
	}


	SECTION("threaded_consumer_producer")
	{
		std::array<std::thread, 2> producers{};
		std::atomic<size_t> finished_producers{};

		std::array<foo, 10'000> data;
		std::atomic<size_t> data_index{};

		// consumer
		auto consumer = std::thread(
			[&]()
			{
				while (finished_producers != producers.size() || !queue.empty())
				{
					if (auto p = queue.try_pop())
					{
						p->touched = true;
					}
					std::this_thread::yield();
				}
			}
		);

		using namespace std::chrono_literals;
		std::this_thread::sleep_for(1ms);

		// producers
		for (auto &producer: producers)
		{
			producer = std::thread(
				[&]()
				{
					for (auto i = data_index++;  i < data.max_size();  i = data_index++)
					{
						queue.push(&data[i]);
						if (i % 147 == 0)
						{
							std::this_thread::sleep_for(1ms);
						}
						else
						{
							std::this_thread::yield();
						}
					}
					finished_producers++;
				}
			);
		}

		std::for_each(producers.begin(), producers.end(),
			[](auto &t)
			{
				t.join();
			}
		);
		consumer.join();

		size_t touched = std::count_if(data.begin(), data.end(),
			[](const auto &f)
			{
				return f.touched;
			}
		);
		CHECK(touched == data.size());
	}


	CHECK(queue.empty());
	CHECK(queue.try_pop() == nullptr);
}


} // namespace
