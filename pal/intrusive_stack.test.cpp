#include <pal/intrusive_stack.hpp>
#include <pal/test.hpp>
#include <catch2/catch_test_macros.hpp>

namespace
{

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
		stack.push(f);
		CHECK(stack.try_pop() == &f);
	}

	SECTION("push/pop: single")
	{
		foo f;
		stack.push(f);
		CHECK(stack.pop() == &f);
	}

	SECTION("push/try_pop: multiple")
	{
		foo f1, f2, f3;
		stack.push(f1);
		stack.push(f2);
		stack.push(f3);

		CHECK(stack.try_pop() == &f3);
		CHECK(stack.try_pop() == &f2);
		CHECK(stack.try_pop() == &f1);
	}

	SECTION("push/pop: multiple")
	{
		foo f1, f2, f3;
		stack.push(f1);
		stack.push(f2);
		stack.push(f3);

		CHECK(stack.pop() == &f3);
		CHECK(stack.pop() == &f2);
		CHECK(stack.pop() == &f1);
	}

	SECTION("push/try_pop: interleaved")
	{
		foo f1, f2, f3;
		stack.push(f1);
		stack.push(f2);
		CHECK(stack.try_pop() == &f2);
		stack.push(f3);
		CHECK(stack.try_pop() == &f3);
		stack.push(f3);
		CHECK(stack.try_pop() == &f3);
		CHECK(stack.try_pop() == &f1);
	}

	SECTION("push/pop: interleaved")
	{
		foo f1, f2, f3;
		stack.push(f1);
		stack.push(f2);
		CHECK(stack.pop() == &f2);
		stack.push(f3);
		CHECK(stack.pop() == &f3);
		stack.push(f3);
		CHECK(stack.pop() == &f3);
		CHECK(stack.pop() == &f1);
	}

	SECTION("move: constructor")
	{
		foo f1, f2;
		stack.push(f1);
		stack.push(f2);

		foo::stack other{std::move(stack)};
		CHECK(stack.empty());
		CHECK(other.pop() == &f2);
		CHECK(other.pop() == &f1);
	}

	SECTION("move: assignment")
	{
		foo f1, f2;
		stack.push(f1);
		stack.push(f2);

		foo::stack other;
		other = std::move(stack);
		CHECK(stack.empty());
		CHECK(other.pop() == &f2);
		CHECK(other.pop() == &f1);
	}

	CHECK(stack.empty());
	CHECK(stack.top() == nullptr);
	CHECK(stack.try_pop() == nullptr);
}

} // namespace
