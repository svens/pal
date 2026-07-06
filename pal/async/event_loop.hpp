#pragma once

/**
 * \file pal/async/event_loop.hpp
 * Per-thread completion loop: a portable run() layer over a backend poll()/wake() primitive
 */

#include <pal/async/task.hpp>
#include <pal/require.hpp>
#include <pal/result.hpp>
#include <pal/version.hpp>
#include <atomic>
#include <chrono>
#include <memory>

namespace pal::async
{

/// Backend sizing knobs: buffer-pool capacity and submission/completion ring depths.
/// Capacities only -- no steering or scheduling policy.
struct event_loop_config
{
	static constexpr size_t default_buffer_count = 8192;
	size_t buffer_count = default_buffer_count;

	static constexpr uint16_t default_buffer_size = 2000;
	uint16_t buffer_size = default_buffer_size;

	size_t submission_depth = 0;
	size_t completion_depth = 0;
};

/// Per-loop observability counters. Only the loop thread mutates them.
struct event_loop_stats
{
	/// Completions dispatched from run()
	uint64_t completions = 0;

	/// Backend wake() deliveries observed by poll()
	uint64_t wakeups = 0;

	/// Multishot completions re-armed in place
	uint64_t rearms = 0;

	/// Iterations that hit the submission-slot cap
	uint64_t submission_exhausted = 0;

	/// Completions dropped under saturation
	uint64_t drops = 0;

	/// Offloaded ops posted but not completed back
	uint64_t offload_in_flight = 0;
};

namespace __event_loop
{

struct impl_type
{
	using clock = std::chrono::steady_clock;

	// backend seam
	size_t (*poll_fn)(impl_type &, clock::duration timeout) noexcept = nullptr;
	void (*wake_fn)(impl_type &) noexcept = nullptr;
	void (*destroy_fn)(impl_type *) noexcept = nullptr;

	// portable state
	clock::time_point now_ = clock::now();
	__task::attorney::task_queue inbox_{};
	std::atomic<size_t> pending_ = 0;
	event_loop_stats stats_{};
	event_loop_config config_{};

	~impl_type () noexcept;

	// portable run() layer
	size_t iterate (clock::duration timeout) noexcept;
	size_t drain_inbox () noexcept;
};

struct deleter
{
	void operator() (impl_type *l) const noexcept
	{
		l->destroy_fn(l);
	}
};

using impl_ptr = std::unique_ptr<impl_type, deleter>;

/// Internal op for \ref event_loop::post -- a bare deferral: run the bound handler on the loop thread. The
/// posted task must be app-managed; \c borrow re-materializes its \c task_ptr at drain and its REQUIRE
/// enforces that contract.
struct op_post
{
	using signature = void(task_ptr &&) noexcept;

	template <typename F>
	static void dispatch (task &t, F &f, std::error_code, size_t) noexcept
	{
		f(t.borrow());
	}
};

/// Enqueue an already-bound task onto the loop's inbox and wake it. Thread-safe.
void post (impl_type &l, task_ptr &&t) noexcept;

} // namespace __event_loop

/// Per-thread completion loop. Not thread-safe and unchecked: drive it (\ref run / \ref run_once /
/// \ref run_for, \ref now, \ref stats) from a single thread at a time -- pin it to one thread or serialize
/// externally. \ref post is the sole thread-safe member.
class event_loop
{
public:

	using clock = std::chrono::steady_clock;

	event_loop (event_loop &&) noexcept = default;
	event_loop &operator= (event_loop &&) noexcept = default;
	~event_loop () noexcept = default;

	/// Monotonic time cached once per run() iteration, so per-operation reads avoid a clock syscall.
	[[nodiscard]] const clock::time_point &now () const noexcept
	{
		return impl_->now_;
	}

	/// Per-loop observability counters.
	[[nodiscard]] const event_loop_stats &stats () const noexcept
	{
		return impl_->stats_;
	}

	/// Run iterations until no work remains; returns the number of completions dispatched.
	result<size_t> run () noexcept;

	/// Poll without blocking: dispatch pending completions; returns the count (0 if none were ready).
	result<size_t> run_once () noexcept;

	/// Run one iteration, blocking at most \a timeout; returns the count (0 on timeout).
	result<size_t> run_for (clock::duration timeout) noexcept;

	/// Run the completion \a handler for \a t on this loop's thread (thread-safe).
	/// \a handler is bound before the task is enqueued (the task is caller-owned, so no data race) and invoked from
	/// a subsequent run() on the loop thread.
	template <typename H>
	void post (task_ptr &&t, H handler) noexcept
		requires __async::handler<H, void(task_ptr &&) noexcept>
	{
		t->bind<__event_loop::op_post>(std::move(handler));
		__event_loop::post(*impl_, std::move(t));
	}

private:

	explicit event_loop (__event_loop::impl_ptr impl) noexcept
		: impl_{std::move(impl)}
	{
	}

	friend result<event_loop> make_loop (const event_loop_config &) noexcept;

	__event_loop::impl_ptr impl_;
};

/// Create an event loop with the default backend for this platform, or an error if the platform has no
/// backend or a resource limit is hit.
result<event_loop> make_loop (const event_loop_config &config = {}) noexcept;

} // namespace pal::async
