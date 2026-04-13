#include <pal/intrusive_queue.hpp>
#include <pal/test.hpp>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <tuple>

namespace
{

TEST_CASE("intrusive_queue")
{
	struct foo
	{
		pal::intrusive_queue_hook<foo> hook{};
		using queue = pal::intrusive_queue<&foo::hook>;
	};
	static_assert(std::is_same_v<foo, foo::queue::value_type>);

	foo::queue queue{};
	CHECK(queue.empty());
	CHECK(queue.head() == nullptr);
	CHECK(queue.try_pop() == nullptr);

	SECTION("move: constructor")
	{
		foo f1, f2;
		queue.push(f1);
		queue.push(f2);

		foo::queue q{std::move(queue)};
		CHECK(queue.empty());
		CHECK(q.try_pop() == &f1);
		CHECK(q.try_pop() == &f2);
		CHECK(q.empty());
	}

	SECTION("move: constructor from empty")
	{
		auto q{std::move(queue)};
		CHECK(q.empty());
	}

	SECTION("move: assignment")
	{
		foo f1, f2;
		queue.push(f1);

		foo::queue q;
		q.push(f2);
		q = std::move(queue);

		CHECK(queue.empty());
		CHECK(q.try_pop() == &f1);
		CHECK(q.empty());
	}

	SECTION("move: assignment to empty")
	{
		foo f1, f2;
		queue.push(f1);
		queue.push(f2);

		foo::queue q;
		q = std::move(queue);
		CHECK(queue.empty());
		CHECK(q.try_pop() == &f1);
		CHECK(q.try_pop() == &f2);
		CHECK(q.empty());
	}

	SECTION("move: assignment from empty")
	{
		foo f1, f2;
		foo::queue q;
		q.push(f1);
		q.push(f2);

		q = std::move(queue);
		CHECK(q.empty());
	}

	SECTION("move: assignment from empty to empty")
	{
		foo::queue q;
		q = std::move(queue);
		CHECK(q.empty());
	}

	SECTION("splice")
	{
		foo f1, f2;
		foo::queue q;
		q.push(f1);
		queue.push(f2);

		q.splice(std::move(queue));
		CHECK(q.try_pop() == &f1);
		CHECK(q.try_pop() == &f2);
		CHECK(q.empty());
	}

	SECTION("splice: to empty")
	{
		foo f1, f2;
		queue.push(f1);
		queue.push(f2);

		foo::queue q;
		q.splice(std::move(queue));
		CHECK(q.try_pop() == &f1);
		CHECK(q.try_pop() == &f2);
		CHECK(q.empty());
	}

	SECTION("splice: from empty")
	{
		foo f1, f2;
		foo::queue q;
		q.push(f1);
		q.push(f2);

		q.splice(std::move(queue));
		CHECK(q.try_pop() == &f1);
		CHECK(q.try_pop() == &f2);
		CHECK(q.empty());
	}

	SECTION("splice: from empty to empty")
	{
		foo::queue q;
		q.splice(std::move(queue));
		CHECK(q.empty());
	}

	SECTION("push/try_pop: single")
	{
		foo f;
		queue.push(f);
		CHECK(queue.try_pop() == &f);
	}

	SECTION("push/pop: single")
	{
		foo f;
		queue.push(f);
		REQUIRE_FALSE(queue.empty());
		CHECK(queue.pop() == &f);
	}

	SECTION("push/try_pop: multiple")
	{
		foo f1, f2, f3;
		queue.push(f1);
		queue.push(f2);
		queue.push(f3);

		CHECK(queue.try_pop() == &f1);
		CHECK(queue.try_pop() == &f2);
		CHECK(queue.try_pop() == &f3);
	}

	SECTION("push/pop: multiple")
	{
		foo f1, f2, f3;
		queue.push(f1);
		queue.push(f2);
		queue.push(f3);

		REQUIRE_FALSE(queue.empty());
		CHECK(queue.pop() == &f1);
		REQUIRE_FALSE(queue.empty());
		CHECK(queue.pop() == &f2);
		REQUIRE_FALSE(queue.empty());
		CHECK(queue.pop() == &f3);
	}

	SECTION("push/try_pop: interleaved")
	{
		foo f1, f2, f3;
		queue.push(f1);
		queue.push(f2);
		CHECK(queue.try_pop() == &f1);
		queue.push(f3);
		CHECK(queue.try_pop() == &f2);
		queue.push(f2);
		CHECK(queue.try_pop() == &f3);
		CHECK(queue.try_pop() == &f2);
	}

	SECTION("push/pop: interleaved")
	{
		foo f1, f2, f3;
		queue.push(f1);
		queue.push(f2);
		REQUIRE_FALSE(queue.empty());
		CHECK(queue.pop() == &f1);
		queue.push(f3);
		REQUIRE_FALSE(queue.empty());
		CHECK(queue.pop() == &f2);
		queue.push(f2);
		REQUIRE_FALSE(queue.empty());
		CHECK(queue.pop() == &f3);
		REQUIRE_FALSE(queue.empty());
		CHECK(queue.pop() == &f2);
	}

	SECTION("iterate: empty")
	{
		CHECK(queue.begin() == queue.end());
		size_t count = 0;
		for (auto &node: queue)
		{
			std::ignore = node;
			count++;
		}
		CHECK(count == 0);
	}

	SECTION("iterate: non-empty")
	{
		foo f1, f2, f3;
		foo::queue q;
		q.push(f1);
		q.push(f2);
		q.push(f3);

		auto it = q.begin();
		CHECK(&*it == &f1);
		CHECK(it->hook == f1.hook);
		++it;
		CHECK(&*it == &f2);
		it++;
		CHECK(&*it == &f3);
		++it;
		CHECK(it == q.end());
	}

	SECTION("iterate: ranges")
	{
		foo f1, f2, f3;
		foo::queue q;
		q.push(f1);
		q.push(f2);
		q.push(f3);

		std::array<foo *, 3> expected = {&f1, &f2, &f3};
		size_t i = 0;
		std::ranges::for_each(
			q,
			[&] (foo &node)
			{
				CHECK(&node == expected[i++]);
			}
		);
		CHECK(i == 3);
	}

	SECTION("insert_sorted")
	{
		struct bar
		{
			pal::intrusive_queue_hook<bar> hook{};
			using queue = pal::intrusive_queue<&bar::hook>;

			constexpr bool operator< (const bar &that) const noexcept
			{
				return this < &that;
			}
		};

		std::array<bar, 4> b;

		bar::queue q;

		SECTION("insert_sorted: head")
		{
			q.insert_sorted(b[2]);
			q.insert_sorted(b[1]);
			q.insert_sorted(b[0]);
		}

		SECTION("insert_sorted: tail")
		{
			q.insert_sorted(b[0]);
			q.insert_sorted(b[1]);
			q.insert_sorted(b[2]);
		}

		SECTION("insert_sorted: mixed")
		{
			q.insert_sorted(b[1]);
			q.insert_sorted(b[2]);
			q.insert_sorted(b[0]);
		}

		q.push(b[3]);

		REQUIRE_FALSE(q.empty());
		CHECK(q.try_pop() == &b[0]);
		REQUIRE_FALSE(q.empty());
		CHECK(q.try_pop() == &b[1]);
		REQUIRE_FALSE(q.empty());
		CHECK(q.try_pop() == &b[2]);
		REQUIRE_FALSE(q.empty());
		CHECK(q.try_pop() == &b[3]);
	}

	CHECK(queue.empty());
	CHECK(queue.head() == nullptr);
	CHECK(queue.try_pop() == nullptr);
}

} // namespace
