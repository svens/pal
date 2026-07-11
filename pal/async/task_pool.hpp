#pragma once

/**
 * \file pal/async/task_pool.hpp
 * Fixed-size pool of tasks with attached payload storage
 */

#include <pal/async/task.hpp>
#include <pal/require.hpp>
#include <array>
#include <utility>

namespace pal::async
{

/// Fixed-size pool of \a TaskCount reusable \ref task, each with \a BufferSize bytes of payload
/// storage attached as its \ref task::span. The default implementation of the app side of the task
/// lifecycle: single-shot operations take app-managed tasks, and this class owns their storage and
/// idle bookkeeping so applications do not have to.
///
/// \ref try_acquire hands out an owning \ref task_ptr; dropping it returns the task to the pool
/// with its payload window reset to the slot's full buffer. Reuse is LIFO: the most recently
/// recycled task is handed out first, keeping its cache lines warm.
///
/// Sizing is compile-time (like \c std::array): construction neither allocates nor fails. Mind the
/// object size when choosing placement -- a pool spans roughly <tt>TaskCount * (sizeof(task) +
/// BufferSize)</tt> bytes, so large pools belong in static or heap-embedded storage, not on the
/// stack. The default \a BufferSize keeps each slot at a power-of-two 2 KiB.
///
/// \note Not thread-safe: acquire and drop tasks on one thread at a time (operations complete on
/// the loop thread, so drops land there naturally).
template <size_t TaskCount, size_t BufferSize = 2048 - sizeof(task)>
class task_pool: private __task::recycler
{
public:

	static_assert(TaskCount > 0, "task_pool without tasks");
	static_assert(BufferSize > 0, "task_pool without payload storage");

	task_pool () noexcept
		: task_pool{std::make_index_sequence<TaskCount>{}}
	{
	}

	/// All tasks must be at rest (no acquired \ref task_ptr outstanding).
	~task_pool () noexcept
	{
		if constexpr (build == build_type::debug)
		{
			auto at_rest = size_t{0};
			while (freelist_.try_pop() != nullptr)
			{
				++at_rest;
			}
			pal_require(at_rest == TaskCount, "task_pool destroyed with tasks in flight");
		}
	}

	task_pool (const task_pool &) = delete;
	task_pool &operator= (const task_pool &) = delete;
	task_pool (task_pool &&) = delete;
	task_pool &operator= (task_pool &&) = delete;

	/// Acquire a task from the pool, or an empty \ref task_ptr when all tasks are in flight
	/// (expected steady-state condition, not an error). The task's payload window spans its full
	/// buffer; dropping the returned \ref task_ptr recycles the task back into this pool.
	[[nodiscard]] task_ptr try_acquire () noexcept
	{
		return task_ptr{freelist_.try_pop()};
	}

private:

	struct alignas(cache_line_size) slot
	{
		task t;
		std::byte buffer[BufferSize];

		explicit slot (__task::recycler &recycle) noexcept
			: t{__task::attorney::make_pool_managed(recycle)}
		{
			t.span(buffer);
		}
	};

	// recycle() recovers the slot from the task address
	static_assert(std::is_standard_layout_v<slot>);

	template <size_t... I>
	explicit task_pool (std::index_sequence<I...>) noexcept
		: __task::recycler{recycle}
		, storage_{{(static_cast<void>(I), slot{*this})...}}
	{
		for (auto &s: storage_)
		{
			freelist_.push(s.t);
		}
	}

	static void recycle (__task::recycler &recycle, task &t) noexcept
	{
		auto &self = static_cast<task_pool &>(recycle);
		auto &s = *reinterpret_cast<slot *>(&t);
		t.span(s.buffer);
		self.freelist_.push(t);
	}

	std::array<slot, TaskCount> storage_;
	__task::attorney::task_stack freelist_{};
};

} // namespace pal::async
