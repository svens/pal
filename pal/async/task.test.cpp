#include <pal/async/task.hpp>
#include <pal/test.hpp>
#include <catch2/catch_test_macros.hpp>
#include <array>

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

	SECTION("borrow() on a pool-managed task is a REQUIRE violation")
	{
		if constexpr (pal::build == pal::build_type::debug)
		{
			counting_recycler r;
			task t = __task::attorney::make_pool_managed(r);
			auto msg = pal_test::require_terminate([&] { std::ignore = t.borrow(); });
			CHECK(msg.contains("pool-managed"));
		}
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
		// clang-format off
		t.bind<op_test>([&](int v) noexcept
		{
			++calls;
			CHECK(v == 7);
		});
		// clang-format on
		t.scratch_as<op_state>() = {.value = 7};
		t.complete();
		CHECK(calls == 1);
	}

	SECTION("move-only task_ptr threads through the erased call")
	{
		task t;
		task_ptr received;
		t.bind<op_move_only>([&received] (task_ptr &&p) noexcept { received = std::move(p); });
		t.complete();
		CHECK(received.get() == &t);
	}
}

} // namespace
