#include <pal/intrusive_queue>
#include <pal/test>
#include <catch2/catch_test_macros.hpp>
#include <array>

namespace {

TEST_CASE("intrusive_queue")
{
	struct foo
	{
		pal::intrusive_queue_hook<foo> hook{};
		using queue = pal::intrusive_queue<&foo::hook>;

		constexpr bool operator< (const foo &that) const noexcept
		{
			// insert_sorted: for tests ordering by location in memory
			return this < &that;
		}
	};
	static_assert(std::is_same_v<foo, foo::queue::value_type>);

	foo::queue queue{};
	CHECK(queue.empty());
	CHECK(queue.head() == nullptr);
	CHECK(queue.try_pop() == nullptr);

	SECTION("ctor(&&)")
	{
		foo f1, f2;
		queue.push(&f1);
		queue.push(&f2);

		auto q{std::move(queue)};
		CHECK(q.try_pop() == &f1);
		CHECK(q.try_pop() == &f2);
		CHECK(q.empty());
	}

	SECTION("ctor(&&) from empty")
	{
		auto q{std::move(queue)};
		CHECK(q.empty());
	}

	SECTION("operator=(&&)")
	{
		foo f1, f2;
		queue.push(&f1);

		foo::queue q;
		q.push(&f2);

		q = std::move(queue);

		CHECK(q.try_pop() == &f1);
		CHECK(q.empty());
	}

	SECTION("operator=(&&) to empty")
	{
		foo f1, f2;
		queue.push(&f1);
		queue.push(&f2);

		foo::queue q;
		q = std::move(queue);
		CHECK(q.try_pop() == &f1);
		CHECK(q.try_pop() == &f2);
		CHECK(q.empty());
	}

	SECTION("operator=(&&) from empty")
	{
		foo f1, f2;
		foo::queue q;
		q.push(&f1);
		q.push(&f2);

		q = std::move(queue);
		CHECK(q.empty());
	}

	SECTION("operator=(&&) from empty to empty")
	{
		foo::queue q;
		q = std::move(queue);
		CHECK(q.empty());
	}

	SECTION("splice")
	{
		foo f1, f2;

		foo::queue q;
		q.push(&f1);

		queue.push(&f2);

		q.splice(std::move(queue));
		CHECK(q.try_pop() == &f1);
		CHECK(q.try_pop() == &f2);
		CHECK(q.empty());
	}

	SECTION("splice to empty")
	{
		foo f1, f2;
		queue.push(&f1);
		queue.push(&f2);

		foo::queue q;
		q.splice(std::move(queue));
		CHECK(q.try_pop() == &f1);
		CHECK(q.try_pop() == &f2);
		CHECK(q.empty());
	}

	SECTION("splice from empty")
	{
		foo f1, f2;
		foo::queue q;
		q.push(&f1);
		q.push(&f2);

		q.splice(std::move(queue));

		CHECK(q.try_pop() == &f1);
		CHECK(q.try_pop() == &f2);
		CHECK(q.empty());
	}

	SECTION("splice from empty to empty")
	{
		foo::queue q;
		q.splice(std::move(queue));
		CHECK(q.empty());
	}

	SECTION("push/try_pop: single")
	{
		foo f;
		queue.push(&f);
		CHECK(queue.try_pop() == &f);
	}

	SECTION("push/pop: single")
	{
		foo f;
		queue.push(&f);
		REQUIRE_FALSE(queue.empty());
		CHECK(queue.pop() == &f);
	}

	SECTION("push/try_pop: multiple")
	{
		foo f1, f2, f3;
		queue.push(&f1);
		queue.push(&f2);
		queue.push(&f3);

		CHECK(queue.try_pop() == &f1);
		CHECK(queue.try_pop() == &f2);
		CHECK(queue.try_pop() == &f3);
	}

	SECTION("push/pop: multiple")
	{
		foo f1, f2, f3;
		queue.push(&f1);
		queue.push(&f2);
		queue.push(&f3);

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
		queue.push(&f1);
		queue.push(&f2);

		// pop 1
		CHECK(queue.try_pop() == &f1);

		// push 3
		queue.push(&f3);

		// pop 2, push 2
		CHECK(queue.try_pop() == &f2);
		queue.push(&f2);

		// pop 3
		CHECK(queue.try_pop() == &f3);

		// pop 2
		CHECK(queue.try_pop() == &f2);
	}

	SECTION("push/pop: interleaved")
	{
		foo f1, f2, f3;
		queue.push(&f1);
		queue.push(&f2);

		// pop 1
		REQUIRE_FALSE(queue.empty());
		CHECK(queue.pop() == &f1);

		// push 3
		queue.push(&f3);

		// pop 2, push 2
		REQUIRE_FALSE(queue.empty());
		CHECK(queue.pop() == &f2);
		queue.push(&f2);

		// pop 3
		REQUIRE_FALSE(queue.empty());
		CHECK(queue.pop() == &f3);

		// pop 2
		REQUIRE_FALSE(queue.empty());
		CHECK(queue.pop() == &f2);
	}

	if constexpr (pal::build == pal::build_type::debug)
	{
		SECTION("push: nullptr")
		{
			foo *f = nullptr;
			CHECK_THROWS_AS(
				queue.push(f),
				std::logic_error
			);
		}
	}

	SECTION("insert_sorted")
	{
		std::array<foo, 4> f;
		static_assert(f[0] < f[1]);
		static_assert(f[1] < f[2]);

		SECTION("head")
		{
			queue.insert_sorted(&f[2]);
			queue.insert_sorted(&f[1]);
			queue.insert_sorted(&f[0]);
		}

		SECTION("tail")
		{
			queue.insert_sorted(&f[0]);
			queue.insert_sorted(&f[1]);
			queue.insert_sorted(&f[2]);
		}

		SECTION("mixed")
		{
			queue.insert_sorted(&f[1]);
			queue.insert_sorted(&f[2]);
			queue.insert_sorted(&f[0]);
		}

		queue.push(&f[3]);

		REQUIRE_FALSE(queue.empty());
		CHECK(queue.try_pop() == &f[0]);

		REQUIRE_FALSE(queue.empty());
		CHECK(queue.try_pop() == &f[1]);

		REQUIRE_FALSE(queue.empty());
		CHECK(queue.try_pop() == &f[2]);

		REQUIRE_FALSE(queue.empty());
		CHECK(queue.try_pop() == &f[3]);
	}

	SECTION("for_each")
	{
		foo f1, f2;
		queue.push(&f1);
		queue.push(&f2);

		size_t count = 0;
		queue.for_each([&](foo &node)
		{
			count++;
			if (count == 1)
			{
				CHECK(&node == &f1);
			}
			else if (count == 2)
			{
				CHECK(&node == &f2);
			}
		});
		CHECK(count == 2);

		CHECK(queue.try_pop() == &f1);
		CHECK(queue.try_pop() == &f2);
	}

	SECTION("for_each: empty")
	{
		bool invoked = false;
		queue.for_each([&invoked](foo &)
		{
			invoked = true;
		});
		CHECK_FALSE(invoked);
	}

	CHECK(queue.empty());
	CHECK(queue.head() == nullptr);
	CHECK(queue.try_pop() == nullptr);
}

} // namespace
