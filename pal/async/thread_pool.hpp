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

/// Create a pool of \a threads worker threads.
/// Errors: zero \a threads, or thread or memory resource exhaustion.
result<thread_pool> make_thread_pool (size_t threads) noexcept;

namespace __thread_pool
{

struct impl_type;

struct deleter
{
	void operator() (impl_type *) const noexcept;
};

using impl_ptr = std::unique_ptr<impl_type, deleter>;

void enqueue (impl_type &pool, __event_loop::impl_type &origin, task &t) noexcept;
void offload_done (__event_loop::impl_type &origin) noexcept;

template <typename Handler>
struct post_back
{
	__event_loop::impl_type *origin;
	[[no_unique_address]] Handler handler;

	void operator() (task_ptr &&t) noexcept
	{
		offload_done(*origin);
		handler(std::move(t));
	}
};

template <typename Work, typename Handler>
struct closure
{
	__event_loop::impl_type *origin;
	[[no_unique_address]] Work work;
	[[no_unique_address]] Handler handler;

	void operator() (task &t) noexcept
	{
		work(t);
		t.bind<__event_loop::op_post>(post_back<Handler>{origin, std::move(handler)});
		__event_loop::post(*origin, task_ptr{&t});
	}
};

template <typename Work, typename Handler>
void submit (impl_type &pool, __event_loop::impl_type &origin, task_ptr &&t, Work work, Handler handler) noexcept
{
	using closure_type = closure<Work, Handler>;
	static_assert(
		sizeof(closure_type) <= __async::closure_capacity,
		"combined work and handler closures exceed the closure budget"
	);
	t->bind<__task::op_plain>(closure_type{&origin, std::move(work), std::move(handler)});
	enqueue(pool, origin, *t.release());
}

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
	/// subsequent run(), like \ref event_loop::post). Loop-thread-only, like \ref event_loop::post_after;
	/// counted in \ref event_loop_stats::offload_in_flight.
	///
	/// \a work borrows the task (the pool retains ownership) and may use its entire scratch; offloaded work
	/// cannot be cancelled or interrupted, and there is no error_code path -- work records its own status in
	/// scratch for \a handler to read. \a handler receives ownership. Both closures share the task's one
	/// inline closure budget, less one pointer. \a loop must outlive the operation: it is destroyable only
	/// once \a handler has run (see class contract).
	///
	/// Worker completion delivery follows \ref event_loop::post backpressure rules.
	template <typename Work, typename Handler>
	void post (task_ptr &&t, event_loop &loop, Work work, Handler handler) noexcept
		requires __async::handler<Work, void(task &) noexcept>
		&& __async::handler<Handler, void(task_ptr &&) noexcept>
	{
		__thread_pool::submit(*impl_, *loop.impl_, std::move(t), std::move(work), std::move(handler));
	}

private:

	explicit thread_pool (__thread_pool::impl_ptr impl) noexcept
		: impl_{std::move(impl)}
	{
	}

	friend result<thread_pool> make_thread_pool (size_t) noexcept;
	friend class event_loop;

	__thread_pool::impl_ptr impl_;
};

} // namespace pal::async
