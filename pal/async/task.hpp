#pragma once

/**
 * \file pal/async/task.hpp
 * Reusable, allocation-free completion carrier
 */

#include <pal/intrusive_queue.hpp>
#include <pal/intrusive_stack.hpp>
#include <pal/version.hpp>
#include <array>
#include <cstddef>
#include <memory>
#include <new>
#include <span>
#include <type_traits>
#include <utility>

namespace pal::async
{

namespace __async
{

/// Alignment of a \ref completion's inline closure storage: pointer alignment on every platform, so the closures
/// a \ref handler accepts do not depend on the toolchain's \c max_align_t.
inline constexpr size_t closure_alignment = alignof(void *);

/// Inline closure storage budget for a \ref completion, in bytes: a cache line minus the pointer-sized \c thunk_
/// in front of it, so \c completion<Carrier> fits one cache line exactly (see static_assert below). Named
/// separately from the per-op scratch budget of \ref pal::async::task so "closure too big" and "op scratch grew"
/// are distinct compile errors.
inline constexpr size_t closure_capacity = cache_line_size - closure_alignment;

template <typename F, typename Signature>
struct signature_t;

template <typename F, typename R, typename... Args>
struct signature_t<F, R(Args...) noexcept>: std::bool_constant<std::is_nothrow_invocable_r_v<R, F, Args...>>
{
};

/// A closure bindable to a \ref completion: inline-storable (trivially copyable/destructible, within the
/// closure budget and alignment) and callable as \a Signature. Lambdas capturing pointers/references/scalars
/// qualify. Checked at the public shell, never re-checked internally. Spell \a Signature literally at the app
/// boundary so the diagnostic names it verbatim; internal seams pass \c Op::signature.
template <typename F, typename Signature>
concept handler = requires
{
	requires std::is_trivially_copyable_v<F>;
	requires std::is_trivially_destructible_v<F>;
	requires sizeof(F) <= closure_capacity;
	requires alignof(F) <= closure_alignment;
	requires signature_t<F, Signature>::value;
};

/// Soft-core completion dispatch unit: one erased "completion thunk" function pointer plus inline closure
/// storage, embedded in \c task (I/O ops, posts, timers). \a Carrier is whatever the completion is embedded in;
/// it is passed to \c Op::dispatch so it can materialize typed arguments (e.g. an endpoint written into scratch
/// state) before invoking the closure.
///
/// \c Op is a type with:
/// \code
/// using signature = void(...) noexcept; // the handler's required call signature
/// static void dispatch (Carrier &carrier, F &f) noexcept;
/// \endcode
///
/// Non-copyable, non-movable: a completion is always accessed in place, embedded in its carrier.
template <typename Carrier>
class completion
{
public:

	completion () noexcept = default;
	~completion () noexcept = default;

	completion (const completion &) = delete;
	completion &operator= (const completion &) = delete;
	completion (completion &&) = delete;
	completion &operator= (completion &&) = delete;

	/// Bind single-shot closure \a f for \a Op. \a f is stored inline now and copied back out at the start of
	/// the matching \ref complete, before \c Op::dispatch invokes it -- so the completion is rebindable again
	/// from inside its own callback.
	template <typename Op>
	void bind (handler<typename Op::signature> auto f) noexcept
	{
		::new (static_cast<void *>(storage_.data())) decltype(f)(std::move(f));
		thunk_ = &single_shot_thunk<Op, decltype(f)>;
	}

	/// Invoke the bound closure via \c Op::dispatch(carrier, f). The closure is copied out before
	/// \c Op::dispatch runs. Each \ref bind is matched by exactly one complete.
	void complete (Carrier &carrier) noexcept
	{
		thunk_(*this, carrier);
	}

private:

	using thunk_type = void (*)(completion &, Carrier &) noexcept;

	template <typename Op, typename F>
	static void single_shot_thunk (completion &self, Carrier &carrier) noexcept
	{
		F f{*std::launder(reinterpret_cast<F *>(self.storage_.data()))};
		Op::dispatch(carrier, f);
	}

	thunk_type thunk_ = nullptr;
	alignas(closure_alignment) std::array<std::byte, closure_capacity> storage_;
};

static_assert(sizeof(completion<std::max_align_t>) == cache_line_size);

} // namespace __async

class task;

namespace __task
{

/// Returns a pool-managed \ref task to its owning pool when its \ref task_ptr drops. A pool embeds one and
/// recovers its own identity from the reference passed to \ref fn. App-managed tasks store no recycler.
struct recycler
{
	void (*fn)(recycler &recycler, task &task) noexcept;
};

/// Mirrors task's non-scratch members, in order, so \c scratch_capacity fills the rest of the cache-line
/// budget without hand-summing sizes (kept honest by the size static_assert below).
struct layout
{
	__async::completion<task> completion;
	union
	{
		intrusive_queue_hook<task> hook;
		intrusive_stack_hook<task> stack_hook;
	};
	recycler *recycle;
	std::span<std::byte> span;
};

constexpr size_t round_up (size_t n, size_t m) noexcept
{
	return (n + m - 1) / m * m;
}

struct op_plain
{
	using signature = void(task &) noexcept;

	template <typename F>
	static void dispatch (task &t, F &f) noexcept
	{
		f(t);
	}
};

struct attorney;

} // namespace __task

/// Deleter for \ref task_ptr: runs the task's own recycler (a no-op for app-managed tasks), so a bare
/// \c task* recovered from a queue reconstructs the same ownership.
struct task_deleter
{
	void operator() (task *t) const noexcept;
};

/// Owning handle to a \ref task; dropping it recycles the task through its stored recycler.
using task_ptr = std::unique_ptr<task, task_deleter>;

/// Per-operation state carrier: result(s) in \ref scratch, completion callback via \ref bind.
///
/// A task carries one operation at a time: from \c start_*, \c post or \c post_after until its handler is
/// invoked, the operation owns the task. Starting another operation on it (e.g. via a second \ref borrow of
/// an app-managed task), or destroying the task or its payload storage before then, is undefined behavior,
/// not checked. Inside the handler the operation is over, so the handler may start the next one on the
/// same task.
class task
{
public:

	/// Alignment of the op scratch storage, the same on every platform (see \ref scratch_as).
	static constexpr size_t scratch_alignment = 16;

	/// Minimum op scratch; the actual budget rounds up to fill the task's last cache line.
	static constexpr size_t min_scratch_capacity = 128;

	/// Op-specific scratch storage (msghdr/iovec/sockaddr_storage, ...): at least \ref min_scratch_capacity bytes,
	/// grown to round a whole task up to a cache-line multiple so the remainder is usable scratch rather than
	/// dead padding.
	static constexpr size_t scratch_capacity =
		__task::round_up(sizeof(__task::layout) + min_scratch_capacity, cache_line_size)
		- sizeof(__task::layout)
	;

	/// Construct an app-managed task (see \ref borrow)
	task () noexcept = default;

	/// Construct an app-managed task with payload storage \a span attached (see \ref span)
	explicit task (std::span<std::byte> span) noexcept
		: span_{span}
	{
	}

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
	void complete () noexcept
	{
		completion_.complete(*this);
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
		static_assert(alignof(T) <= scratch_alignment, "T over-aligned for scratch");
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

	/// This task's payload window: storage where operations read and write payload data (received bytes,
	/// resolved endpoints, ...), distinct from \ref scratch, which ops clobber with their own state. On
	/// completion the op adjusts the window to the storage actually used.
	[[nodiscard]] std::span<std::byte> span () const noexcept
	{
		return span_;
	}

	/// Point this task's payload window at \a span. Adjust only while the task is at rest -- an op in
	/// flight owns the window. Dropping a pool-managed task resets its window to the slot's full buffer;
	/// app-managed windows stay as the app last set them.
	void span (std::span<std::byte> span) noexcept
	{
		span_ = span;
	}

	/// Yield an owning \c task_ptr to this app-managed task. Its recycler is null, so dropping the returned
	/// \c task_ptr leaves the task untouched for the app to reuse.
	[[nodiscard]] task_ptr borrow () noexcept
	{
		return task_ptr{this};
	}

private:

	explicit task (__task::recycler &recycle) noexcept
		: recycle_{&recycle}
	{
	}

	void recycle () noexcept
	{
		if (recycle_ != nullptr)
		{
			recycle_->fn(*recycle_, *this);
		}
	}

	__async::completion<task> completion_;

	// A task is in at most one queue at a time, so both hook flavors share storage;
	// which member is active is library-internal knowledge, held per owning queue
	union
	{
		intrusive_queue_hook<task> hook_{};
		intrusive_stack_hook<task> stack_hook_;
	};

	__task::recycler *const recycle_ = nullptr;
	std::span<std::byte> span_{};

	// Last member so it absorbs the task's tail padding into usable space (see \ref scratch_capacity).
	alignas(scratch_alignment) std::array<std::byte, scratch_capacity> scratch_{};

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

/// Grants the loop internals access to task internals kept off its public API.
struct __task::attorney
{
	static task make_pool_managed (recycler &recycle) noexcept
	{
		return task{recycle};
	}

	[[nodiscard]] static recycler *recycler_of (const task &t) noexcept
	{
		return t.recycle_;
	}

	// Kept internal: the hooks stay single-owner for the library.
	// Apps queue their own idle tasks via task::scratch_as() instead.
	using task_queue = pal::intrusive_queue<&task::hook_>;
	using task_stack = pal::intrusive_stack<&task::stack_hook_>;
};

} // namespace pal::async
