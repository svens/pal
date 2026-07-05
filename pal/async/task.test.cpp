#include <pal/async/task.hpp>
#include <pal/test.hpp>
#include <catch2/catch_test_macros.hpp>
#include <system_error>

namespace
{

using namespace pal::async;

struct op_test
{
	template <typename F>
	static constexpr bool invocable = std::is_nothrow_invocable_v<F, int, std::error_code>;

	template <typename F>
	static void dispatch (task &, F &f, std::error_code ec, size_t n) noexcept
	{
		f(static_cast<int>(n), ec);
	}
};

struct record_call
{
	int *calls;
	std::error_code *seen_ec;

	void operator() (int v, std::error_code ec) const noexcept
	{
		++*calls;
		if (seen_ec != nullptr)
		{
			*seen_ec = ec;
		}
		CHECK(v == 7);
	}
};

// Op whose handler takes a move-only task_ptr&&, materialized by dispatch from
// the carrier itself -- proves task_ptr threads through the erased completion
// call without ever living inside the (trivially copyable only) closure storage.
struct op_move_only
{
	template <typename F>
	static constexpr bool invocable = std::is_nothrow_invocable_v<F, task_ptr &&, std::error_code>;

	template <typename F>
	static void dispatch (task &t, F &f, std::error_code ec, size_t) noexcept
	{
		f(t.borrow(), ec);
	}
};

struct receive_task_ptr
{
	task_ptr *received;

	void operator() (task_ptr &&p, std::error_code) const noexcept
	{
		*received = std::move(p);
	}
};

TEST_CASE("async/task")
{
	SECTION("scratch() returns task::scratch_capacity bytes")
	{
		task t;
		CHECK(t.scratch().size() == task::scratch_capacity);
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

	SECTION("borrow() on a loop-managed task is a REQUIRE violation")
	{
		if constexpr (pal::build == pal::build_type::debug)
		{
			task t = __task::attorney::make_loop_managed();
			auto msg = pal_test::require_terminate([&] { std::ignore = t.borrow(); });
			CHECK(msg.contains("loop-managed"));
		}
	}

	SECTION("bind/complete round-trip through Op::dispatch")
	{
		task t;
		int calls = 0;
		std::error_code seen_ec = std::make_error_code(std::errc::io_error);
		t.bind<op_test>(record_call{.calls = &calls, .seen_ec = &seen_ec});
		t.complete({}, 7);
		CHECK(calls == 1);
		CHECK_FALSE(seen_ec);
	}

	SECTION("move-only task_ptr threads through the erased call")
	{
		task t;
		task_ptr received;
		t.bind<op_move_only>(receive_task_ptr{.received = &received});
		t.complete({}, 0);
		CHECK(received.get() == &t);
	}
}

} // namespace
