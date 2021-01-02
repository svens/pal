#include <pal/intrusive_queue>
#include <pal/test>


namespace {


TEST_CASE("intrusive_queue")
{
	struct foo
	{
		pal::intrusive_queue_hook<foo> hook{};
		using queue = pal::intrusive_queue<&foo::hook>;
	};

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


	SECTION("single_push_pop")
	{
		foo f;
		queue.push(&f);
		REQUIRE_FALSE(queue.empty());
		CHECK(queue.head() == &f);
		CHECK(queue.try_pop() == &f);
	}


	SECTION("multiple_push_pop")
	{
		foo f1, f2, f3;
		queue.push(&f1);
		queue.push(&f2);
		queue.push(&f3);

		REQUIRE_FALSE(queue.empty());
		CHECK(queue.head() == &f1);
		CHECK(queue.try_pop() == &f1);

		REQUIRE_FALSE(queue.empty());
		CHECK(queue.head() == &f2);
		CHECK(queue.try_pop() == &f2);

		REQUIRE_FALSE(queue.empty());
		CHECK(queue.head() == &f3);
		CHECK(queue.try_pop() == &f3);
	}


	SECTION("interleaved_push_pop")
	{
		foo f1, f2, f3;
		queue.push(&f1);
		queue.push(&f2);

		// pop 1
		REQUIRE_FALSE(queue.empty());
		CHECK(queue.head() == &f1);
		CHECK(queue.try_pop() == &f1);

		// push 3
		queue.push(&f3);

		// pop 2, push 2
		REQUIRE_FALSE(queue.empty());
		CHECK(queue.head() == &f2);
		CHECK(queue.try_pop() == &f2);
		queue.push(&f2);

		// pop 3
		REQUIRE_FALSE(queue.empty());
		CHECK(queue.head() == &f3);
		CHECK(queue.try_pop() == &f3);

		// pop 2
		REQUIRE_FALSE(queue.empty());
		CHECK(queue.head() == &f2);
		CHECK(queue.try_pop() == &f2);
	}


	if constexpr (!pal::assert_noexcept)
	{
		SECTION("push null")
		{
			foo *f = nullptr;
			CHECK_THROWS_AS(
				queue.push(f),
				std::logic_error
			);
		}
	}


	SECTION("for_each empty")
	{
		bool invoked = false;
		queue.for_each([&invoked](foo &)
		{
			invoked = true;
		});
		CHECK_FALSE(invoked);
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


	CHECK(queue.empty());
	CHECK(queue.head() == nullptr);
	CHECK(queue.try_pop() == nullptr);
}


} // namespace
