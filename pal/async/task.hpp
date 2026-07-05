#pragma once

/**
 * \file pal/async/task.hpp
 * Reusable, allocation-free completion carrier
 */

#include <pal/async/__async.hpp>
#include <pal/require.hpp>
#include <cstddef>
#include <memory>
#include <span>
#include <system_error>
#include <type_traits>
#include <utility>

namespace pal::async
{

namespace __task
{
struct attorney;
}

class task;

/// Deleter for \ref task_ptr. \a recycle is null for app-managed tasks (no-op);
/// the loop's buffer pool sets it to a pool-returning function for loop-managed tasks.
struct task_recycler
{
	void (*recycle)(task *) noexcept = nullptr;

	void operator() (task *t) const noexcept
	{
		if (recycle != nullptr)
		{
			recycle(t);
		}
	}
};

/// Owning handle to a \ref task; \ref task_recycler defines what dropping it does.
using task_ptr = std::unique_ptr<task, task_recycler>;

/// Per-operation state carrier: result(s) in \ref scratch, completion callback via \ref bind.
class task
{
public:

	/// This task's op-specific scratch storage budget (msghdr/iovec/sockaddr_storage, ...): at least 64 bytes,
	/// rounded up to keep a whole task a cache-line multiple, so the rounding remainder is usable scratch
	/// instead of dead padding (see static_assert below).
	static constexpr size_t scratch_capacity = 64
		+ (64 - (sizeof(__async::completion<task>) + sizeof(bool) + 64) % 64) % 64;

	/// Construct an app-managed task (see \ref borrow)
	task () noexcept = default;

	~task () noexcept = default;

	task (const task &) = delete;
	task &operator= (const task &) = delete;
	task (task &&) = delete;
	task &operator= (task &&) = delete;

	/// \see __async::completion::bind
	template <typename Op>
	void bind (auto f) noexcept
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

	/// Yield a non-owning \c task_ptr to this app-managed task; its recycler is a no-op, so dropping the returned
	/// \c task_ptr leaves the task untouched for the app to reuse.
	[[nodiscard]] task_ptr borrow () noexcept
	{
		pal_require(!is_loop_owned_, "borrow() on loop-managed task");
		return task_ptr{this, task_recycler{}};
	}

private:

	explicit task (bool is_loop_owned) noexcept
		: is_loop_owned_{is_loop_owned}
	{
	}

	__async::completion<task> completion_;
	alignas(std::max_align_t) std::byte scratch_[scratch_capacity]{};
	const bool is_loop_owned_ = false;

	friend struct __task::attorney;
};

static_assert(sizeof(task) % 64 == 0);
static_assert(std::is_standard_layout_v<task>);

namespace __task
{

/// Grants access to construct a loop-managed \ref task, without exposing the lifecycle tag on task's public API.
struct attorney
{
	static task make_loop_managed () noexcept
	{
		return task{true};
	}
};

} // namespace __task

} // namespace pal::async
