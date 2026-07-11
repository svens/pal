#pragma once

/**
 * \file pal/async/task.hpp
 * Reusable, allocation-free completion carrier
 */

#include <pal/async/__async.hpp>
#include <pal/intrusive_mpsc_queue.hpp>
#include <pal/intrusive_queue.hpp>
#include <pal/require.hpp>
#include <array>
#include <memory>
#include <span>

namespace pal::async
{

class task;

/// Deleter for \ref task_ptr: runs the task's own recycler (a no-op for app-managed tasks), so a bare
/// \c task* recovered from a queue reconstructs the same ownership.
struct task_deleter
{
	void operator() (task *t) const noexcept;
};

/// Owning handle to a \ref task; dropping it recycles the task through its stored recycler.
using task_ptr = std::unique_ptr<task, task_deleter>;

namespace __task
{

/// Returns a loop-managed \ref task to its pool. Stored on the task; null on app-managed tasks, which own no
/// pool storage.
using recycle_fn = void (*)(task *) noexcept;

/// Mirrors task's non-scratch members, in order, so \c scratch_capacity fills the rest of the cache-line
/// budget without hand-summing sizes (kept honest by the size static_assert below).
struct layout
{
	__async::completion<task> completion;
	union
	{
		intrusive_mpsc_queue_hook<task> mpsc_hook;
		intrusive_queue_hook<task> hook;
	};
	recycle_fn recycle;
};

constexpr size_t round_up (size_t n, size_t m) noexcept
{
	return (n + m - 1) / m * m;
}

struct attorney;

} // namespace __task

/// Per-operation state carrier: result(s) in \ref scratch, completion callback via \ref bind.
class task
{
public:

	/// Minimum op scratch; the actual budget rounds up to fill the task's last cache line.
	static constexpr size_t min_scratch_capacity = 64;

	/// Op-specific scratch storage (msghdr/iovec/sockaddr_storage, ...): at least \ref min_scratch_capacity bytes,
	/// grown to round a whole task up to a cache-line multiple so the remainder is usable scratch rather than
	/// dead padding.
	static constexpr size_t scratch_capacity =
		__task::round_up(sizeof(__task::layout) + min_scratch_capacity, cache_line_size)
		- sizeof(__task::layout);

	/// Construct an app-managed task (see \ref borrow)
	task () noexcept = default;

	~task () noexcept = default;

	task (const task &) = delete;
	task &operator= (const task &) = delete;
	task (task &&) = delete;
	task &operator= (task &&) = delete;

	/// \see __async::completion::bind
	template <typename Op>
	void bind (__async::handler<typename Op::signature> auto f) noexcept
	{
		completion_.bind<Op>(std::move(f));
	}

	/// \see __async::completion::complete
	void complete (std::error_code ec, size_t n) noexcept
	{
		completion_.complete(*this, ec, n);
	}

	/// This task's op-specific scratch storage.
	[[nodiscard]] std::span<std::byte> scratch () noexcept
	{
		return scratch_;
	}

	/// View the scratch storage as a \a T. Valid only while the task is app-owned and at rest: an in-flight
	/// op overwrites scratch with op state, so a \a T placed here does not survive one. Lets an app thread
	/// its own idle tasks onto its own freelist/queue (a hook in \a T) without wrapping \c task.
	template <typename T>
	[[nodiscard]] T &scratch_as () noexcept
	{
		static_assert(sizeof(T) <= scratch_capacity, "T does not fit the scratch budget");
		static_assert(alignof(T) <= alignof(std::max_align_t), "T over-aligned for scratch");
		#ifdef __cpp_lib_start_lifetime_as
		{
			return *std::start_lifetime_as<T>(scratch_.data());
		}
		#else
		{
			// TODO(svens): fallback until every stdlib ships start_lifetime_as
			return *reinterpret_cast<T *>(scratch_.data());
		}
		#endif
	}

	/// Yield an owning \c task_ptr to this app-managed task. Its recycler is null, so dropping the returned
	/// \c task_ptr leaves the task untouched for the app to reuse.
	[[nodiscard]] task_ptr borrow () noexcept
	{
		pal_require(recycle_ == nullptr, "borrow() on loop-managed task");
		return task_ptr{this};
	}

private:

	explicit task (__task::recycle_fn recycle) noexcept
		: recycle_{recycle}
	{
	}

	void recycle () noexcept
	{
		if (recycle_ != nullptr)
		{
			recycle_(this);
		}
	}

	__async::completion<task> completion_;

	// A task is in at most one queue at a time, so both hook flavors share storage;
	// which member is active is library-internal knowledge, held per owning queue
	union
	{
		intrusive_mpsc_queue_hook<task> mpsc_hook_{};
		intrusive_queue_hook<task> hook_;
	};

	const __task::recycle_fn recycle_ = nullptr;

	// Last member so it absorbs the task's tail padding into usable space (see \ref scratch_capacity).
	alignas(std::max_align_t) std::array<std::byte, scratch_capacity> scratch_{};

	friend struct task_deleter;
	friend struct __task::attorney;
};

inline void task_deleter::operator() (task *t) const noexcept
{
	t->recycle();
}

static_assert(sizeof(task) == sizeof(__task::layout) + task::scratch_capacity);
static_assert(sizeof(task) % cache_line_size == 0);
static_assert(std::is_standard_layout_v<task>);

namespace __task
{

/// Grants the loop internals access to task internals kept off its public API.
struct attorney
{
	static task make_loop_managed (recycle_fn recycle) noexcept
	{
		return task{recycle};
	}

	// Kept internal: the hooks stay single-owner for the library.
	// Apps queue their own idle tasks via task::scratch_as() instead.
	using task_mpsc_queue = pal::intrusive_mpsc_queue<&task::mpsc_hook_>;
	using task_queue = pal::intrusive_queue<&task::hook_>;
};

} // namespace __task

} // namespace pal::async
