#include <pal/intrusive_stack>
#include <pal/test>
#include <catch2/catch_test_macros.hpp>

namespace {

TEST_CASE("intrusive_stack")
{
	struct foo
	{
		pal::intrusive_stack_hook<foo> hook{};
		using stack = pal::intrusive_stack<&foo::hook>;
	};
	static_assert(std::is_same_v<foo, foo::stack::value_type>);

	foo::stack stack{};
	CHECK(stack.empty());
	CHECK(stack.top() == nullptr);
	CHECK(stack.try_pop() == nullptr);

	SECTION("push/try_pop: single")
	{
		foo f;
		stack.push(&f);
		CHECK(stack.try_pop() == &f);
	}

	SECTION("push/pop: single")
	{
		foo f;
		stack.push(&f);
		CHECK(stack.pop() == &f);
	}

	SECTION("push/try_pop: multiple")
	{
		foo f1, f2, f3;
		stack.push(&f1);
		stack.push(&f2);
		stack.push(&f3);

		CHECK(stack.try_pop() == &f3);
		CHECK(stack.try_pop() == &f2);
		CHECK(stack.try_pop() == &f1);
	}

	SECTION("push/pop: multiple")
	{
		foo f1, f2, f3;
		stack.push(&f1);
		stack.push(&f2);
		stack.push(&f3);

		CHECK(stack.pop() == &f3);
		CHECK(stack.pop() == &f2);
		CHECK(stack.pop() == &f1);
	}

	SECTION("push/try_pop: interleaved")
	{
		// push 1, push 2
		foo f1, f2, f3;
		stack.push(&f1);
		stack.push(&f2);

		// pop 2
		CHECK(stack.try_pop() == &f2);

		// push 3
		stack.push(&f3);

		// pop 3, push 3
		CHECK(stack.try_pop() == &f3);
		stack.push(&f3);

		// pop 3
		CHECK(stack.try_pop() == &f3);

		// pop 1
		CHECK(stack.try_pop() == &f1);
	}

	SECTION("push/pop: interleaved")
	{
		// push 1, push 2
		foo f1, f2, f3;
		stack.push(&f1);
		stack.push(&f2);

		// pop 2
		CHECK(stack.pop() == &f2);

		// push 3
		stack.push(&f3);

		// pop 3, push 3
		CHECK(stack.pop() == &f3);
		stack.push(&f3);

		// pop 3
		CHECK(stack.pop() == &f3);

		// pop 1
		CHECK(stack.pop() == &f1);
	}

	if constexpr (pal::build == pal::build_type::debug)
	{
		SECTION("push: nullptr")
		{
			foo *f = nullptr;
			CHECK_THROWS_AS(
				stack.push(f),
				std::logic_error
			);
		}
	}

	CHECK(stack.empty());
	CHECK(stack.top() == nullptr);
	CHECK(stack.try_pop() == nullptr);
}

} // namespace
