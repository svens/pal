#include <pal/async/task.hpp>
#include <pal/test.hpp>
#include <catch2/catch_test_macros.hpp>
#include <array>
#include <cstddef>
#include <string>

namespace
{

using namespace pal::async;

struct counting_recycler: __task::recycler
{
	int calls = 0;

	counting_recycler () noexcept
		: __task::recycler{count}
	{
	}

	static void count (__task::recycler &r, task &) noexcept
	{
		++static_cast<counting_recycler &>(r).calls;
	}
};

struct op_state
{
	int value;
};

struct op_test
{
	using signature = void(int) noexcept;

	template <typename F>
	static void dispatch (task &t, F &f) noexcept
	{
		f(t.scratch_as<op_state>().value);
	}
};

// Op whose handler takes a move-only task_ptr&&, materialized by dispatch from
// the carrier itself -- proves task_ptr threads through the erased completion
// call without ever living inside the (trivially copyable only) closure storage.
struct op_move_only
{
	using signature = void(task_ptr &&) noexcept;

	template <typename F>
	static void dispatch (task &t, F &f) noexcept
	{
		f(t.borrow());
	}
};

TEST_CASE("async/task")
{
	SECTION("scratch() returns task::scratch_capacity bytes")
	{
		task t;
		CHECK(t.scratch().size() == task::scratch_capacity);
	}

	SECTION("scratch_as<T> views the scratch storage as a T")
	{
		struct node
		{
			task *next;
			int value;
		};
		static_assert(sizeof(node) <= task::scratch_capacity);

		task t;
		t.scratch_as<node>().next = &t;
		t.scratch_as<node>().value = 42;

		// Same storage as scratch(), and values survive across calls (bytes are reused, not reset).
		CHECK(static_cast<void *>(&t.scratch_as<node>()) == static_cast<void *>(t.scratch().data()));
		CHECK(t.scratch_as<node>().next == &t);
		CHECK(t.scratch_as<node>().value == 42);
	}

	SECTION("borrow() yields a non-owning task_ptr; drop leaves the task usable")
	{
		task t;
		{
			const task_ptr p = t.borrow();
			CHECK(p.get() == &t);
		}
		// recycler was a no-op; the task is unaffected and can be borrowed again
		const task_ptr p2 = t.borrow();
		CHECK(p2.get() == &t);
	}

	SECTION("span() is empty unless payload storage is attached")
	{
		const task t{};
		CHECK(t.span().empty());
	}

	SECTION("span() views storage attached at construction")
	{
		std::array<std::byte, 8> storage{};
		const task t{storage};
		CHECK(t.span().data() == storage.data());
		CHECK(t.span().size() == storage.size());
	}

	SECTION("span(span) adjusts the payload window at rest")
	{
		std::array<std::byte, 8> storage{};
		task t{storage};
		t.span(t.span().subspan(2, 4));
		CHECK(t.span().data() == storage.data() + 2);
		CHECK(t.span().size() == 4);
	}

	SECTION("span survives scratch use")
	{
		std::array<std::byte, 8> storage{};
		task t{storage};
		t.scratch_as<std::array<std::byte, task::scratch_capacity>>().fill(std::byte{0xa5});
		CHECK(t.span().data() == storage.data());
		CHECK(t.span().size() == storage.size());
	}

	SECTION("dropping a pool-managed task_ptr runs its recycler")
	{
		counting_recycler r;
		task t = __task::attorney::make_pool_managed(r);
		{
			const task_ptr p{&t};
			CHECK(p.get() == &t);
		}
		CHECK(r.calls == 1);
	}

	SECTION("bind/complete round-trip through Op::dispatch")
	{
		task t;
		int calls = 0;
		t.bind<op_test>([&](int v) noexcept
		{
			++calls;
			CHECK(v == 7);
		});
		t.scratch_as<op_state>() = {.value = 7};
		t.complete();
		CHECK(calls == 1);
	}

	SECTION("move-only task_ptr threads through the erased call")
	{
		task t;
		task_ptr received;
		t.bind<op_move_only>([&received](task_ptr &&p) noexcept { received = std::move(p); });
		t.complete();
		CHECK(received.get() == &t);
	}
}

struct carrier
{
	__async::completion<carrier> completion;
	int value = 0;
};

struct carrier_op
{
	using signature = void(int) noexcept;

	template <typename F>
	static void dispatch (carrier &c, F &f) noexcept
	{
		f(c.value);
	}
};

TEST_CASE("async/task: completion")
{
	carrier c;

	SECTION("bind and complete invokes the closure once")
	{
		int calls = 0, seen_value = -1;
		c.completion.bind<carrier_op>([&](int v) noexcept
		{
			++calls;
			seen_value = v;
		});
		c.value = 5;
		c.completion.complete(c);
		CHECK(calls == 1);
		CHECK(seen_value == 5);
	}

	SECTION("rebind inside own callback is reused on the next complete")
	{
		int first_calls = 0, second_calls = 0;

		c.completion.bind<carrier_op>([&](int) noexcept
		{
			++first_calls;
			c.completion.bind<carrier_op>([&second_calls](int) noexcept
			{
				++second_calls;
			});
		});

		c.completion.complete(c);
		CHECK(first_calls == 1);
		CHECK(second_calls == 0);

		c.completion.complete(c);
		CHECK(second_calls == 1);
	}
}

static_assert(__async::handler<decltype([](int) noexcept {}), carrier_op::signature>);

[[maybe_unused]] void capturing_lambda_is_a_handler (int &state)
{
	static_assert(__async::handler<decltype([&state](int) noexcept { ++state; }), carrier_op::signature>);
	static_assert(__async::handler<decltype([p = &state](int) noexcept { ++*p; }), carrier_op::signature>);
}

struct no_default_ctor
{
	explicit no_default_ctor (int) noexcept
	{
	}
	void operator() (int) const noexcept
	{
	}
};
static_assert(__async::handler<no_default_ctor, carrier_op::signature>);

struct not_trivially_copyable
{
	std::string s;
	void operator() (int) const noexcept
	{
	}
};
static_assert(!__async::handler<not_trivially_copyable, carrier_op::signature>);

struct too_big
{
	std::byte padding[__async::closure_capacity + 1];
	void operator() (int) const noexcept
	{
	}
};
static_assert(!__async::handler<too_big, carrier_op::signature>);

struct over_aligned
{
	alignas(alignof(std::max_align_t) * 2) std::byte b;
	void operator() (int) const noexcept
	{
	}
};
static_assert(!__async::handler<over_aligned, carrier_op::signature>);

struct wrong_signature
{
	void operator() () const noexcept
	{
	}
};
static_assert(!__async::handler<wrong_signature, carrier_op::signature>);

struct throwing_call
{
	void operator() (int) const
	{
	}
};
static_assert(!__async::handler<throwing_call, carrier_op::signature>);

} // namespace
