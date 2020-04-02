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


	CHECK(queue.empty());
	CHECK(queue.head() == nullptr);
	CHECK(queue.try_pop() == nullptr);
}


} // namespace
