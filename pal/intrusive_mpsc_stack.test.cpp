#include <pal/intrusive_mpsc_stack.hpp>
#include <pal/test.hpp>
#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <array>
#include <thread>

namespace
{

TEST_CASE("intrusive_mpsc_stack")
{
	struct foo
	{
		pal::intrusive_mpsc_stack_hook<foo> hook;
		using stack = pal::intrusive_mpsc_stack<&foo::hook>;

		int delivery_count = 0;
	};
	static_assert(std::is_same_v<foo, foo::stack::value_type>);

	foo::stack stack{};
	CHECK(stack.empty());
	CHECK(stack.try_pop() == nullptr);

	SECTION("push/try_pop: single")
	{
		foo f;
		stack.push(f);
		REQUIRE_FALSE(stack.empty());
		CHECK(stack.try_pop() == &f);
	}

	SECTION("push/try_pop: multiple")
	{
		foo f1, f2, f3;
		stack.push(f1);
		stack.push(f2);
		stack.push(f3);

		REQUIRE_FALSE(stack.empty());
		CHECK(stack.try_pop() == &f3);
		REQUIRE_FALSE(stack.empty());
		CHECK(stack.try_pop() == &f2);
		REQUIRE_FALSE(stack.empty());
		CHECK(stack.try_pop() == &f1);
	}

	SECTION("push/try_pop: interleaved")
	{
		foo f1, f2, f3;
		stack.push(f1);
		stack.push(f2);

		REQUIRE_FALSE(stack.empty());
		CHECK(stack.try_pop() == &f2);

		stack.push(f3);

		REQUIRE_FALSE(stack.empty());
		CHECK(stack.try_pop() == &f3);

		REQUIRE_FALSE(stack.empty());
		CHECK(stack.try_pop() == &f1);
	}

	SECTION("threaded")
	{
		std::array<foo, 10'000> data;
		std::atomic<bool> producer_done{};

		// clang-format off

		auto consumer = std::thread([&]
		{
			while (!producer_done || !stack.empty())
			{
				if (auto *p = stack.try_pop())
				{
					p->delivery_count++;
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
			stack.push(f);
			std::this_thread::yield();
		}
		producer_done = true;

		// clang-format on

		consumer.join();

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

	CHECK(stack.empty());
	CHECK(stack.try_pop() == nullptr);
}

} // namespace
