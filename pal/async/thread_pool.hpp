#pragma once

/**
 * \file pal/async/thread_pool.hpp
 * Offload plane: worker threads running blocking work, completing back on an event_loop
 */

#include <pal/async/event_loop.hpp>
#include <pal/result.hpp>
#include <memory>
#include <utility>

namespace pal::async
{

class thread_pool;

/// Create a pool of \a threads worker threads (at least one; zero is a precondition violation).
/// Errors: thread or memory resource exhaustion.
result<thread_pool> make_thread_pool (size_t threads) noexcept;

namespace __thread_pool
{

struct impl_type;

struct deleter
{
	void operator() (impl_type *) const noexcept;
};

using impl_ptr = std::unique_ptr<impl_type, deleter>;

/// Post-back target, written into task scratch by \ref thread_pool::post. The worker copies it out before
/// invoking the work closure, which is then free to overwrite the entire scratch.
struct record
{
	__event_loop::impl_type *origin;
};

/// Internal op for \ref thread_pool::post: invoke the composite work + post-back closure on a worker thread.
struct op_execute
{
	using signature = void(task &) noexcept;

	template <typename F>
	static void dispatch (task &t, F &f, std::error_code, size_t) noexcept
	{
		f(t);
	}
};

/// Single-shot composite bound at post time: run the blocking work, then rebind the task with the loop-side
/// handler, so the worker hands an already-bound task to the origin loop's inbox.
template <typename Work, typename Handler>
struct closure
{
	Work work;
	Handler handler;

	void operator() (task &t) noexcept
	{
		work(t);
		t.bind<__event_loop::op_post>(std::move(handler));
	}
};

/// Enqueue an already-bound task for worker execution. Thread-safe.
void submit (impl_type &pool, task &t) noexcept;

} // namespace __thread_pool

/// Pool of worker threads for blocking work that no poller syscall covers (name resolution, file I/O on
/// reactor backends, any deliberately blocking call): \ref post runs a work closure on a worker thread, then
/// a handler closure on the posting loop's thread. Created and owned by the application, shared across any
/// number of loops; the pool knows no loop beyond the per-task post-back target.
///
/// Work arrives at session-setup rates, not packet rates: one shared injection queue, no work stealing. The
/// head-of-line hazard across op classes (a DNS timeout starving file I/O) is solved by wiring, not
/// scheduling: give each op class its own pool.
///
/// Destruction requires an empty queue (debug REQUIRE) and joins the workers, so it blocks on work still in
/// flight. Per the teardown contract the application quiesces first: stops posting, runs its loops until
/// every offloaded handler has completed, then destroys handles, loops and pools, in that order.
class thread_pool
{
public:

	thread_pool (thread_pool &&) noexcept = default;
	thread_pool &operator= (thread_pool &&) noexcept = default;
	~thread_pool () noexcept = default;

	/// Run \a work on one of this pool's worker threads, then \a handler on \a loop's thread (from a
	/// subsequent run(), like \ref event_loop::post). Thread-safe.
	///
	/// \a work borrows the task (the pool retains ownership) and may use its entire scratch; offloaded work
	/// cannot be cancelled or interrupted, and there is no error_code path -- work records its own status in
	/// scratch for \a handler to read. \a handler receives ownership. Both closures share the task's one
	/// inline closure budget. \a loop must outlive the operation: it is destroyable only once \a handler has
	/// run (see class contract).
	template <typename Work, typename Handler>
	void post (event_loop &loop, task_ptr &&t, Work work, Handler handler) noexcept
		requires __async::handler<Work, void(task &) noexcept>
		&& __async::handler<Handler, void(task_ptr &&) noexcept>
	{
		using closure_type = __thread_pool::closure<Work, Handler>;
		static_assert(
			sizeof(closure_type) <= __async::closure_capacity,
			"combined work and handler closures exceed the closure budget"
		);
		t->scratch_as<__thread_pool::record>() = {.origin = loop.impl_.get()};
		t->bind<__thread_pool::op_execute>(closure_type{std::move(work), std::move(handler)});
		__thread_pool::submit(*impl_, *t.release());
	}

private:

	explicit thread_pool (__thread_pool::impl_ptr impl) noexcept
		: impl_{std::move(impl)}
	{
	}

	friend result<thread_pool> make_thread_pool (size_t) noexcept;

	__thread_pool::impl_ptr impl_;
};

} // namespace pal::async
