#pragma once

/**
 * \file pal/async/event_loop.hpp
 * Per-thread completion loop: a portable run() layer over a backend poll()/post() primitive
 */

#include <pal/async/task.hpp>
#include <pal/intrusive_queue.hpp>
#include <pal/result.hpp>
#include <array>
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

namespace pal::async
{

class event_loop;
class thread_pool;

template <typename T>
class handle;

/// Backend tuning knobs. Nothing here selects which resource runs next.
struct event_loop_config
{
	size_t buffer_count = 8192;
	uint16_t buffer_size = 2000;

	/// Operations a backend accepts before one must complete.
	/// Must be in [1, \ref max_submission_depth].
	static constexpr size_t max_submission_depth = 32768;
	size_t submission_depth = 256;

	/// Completions a backend holds before it stalls.
	/// Must be in [\ref submission_depth, \ref max_completion_depth].
	static constexpr size_t max_completion_depth = 65536;
	size_t completion_depth = 512;

	/// Maximum readiness events harvested per backend poll.
	/// Must be in [1, \ref max_poll_batch].
	static constexpr size_t max_poll_batch = 1024;
	size_t poll_batch = 64;

	/// Maximum operations completed per socket direction per drain visit: the loop's fairness quantum.
	/// Must be in [1, \ref max_drain_batch].
	static constexpr size_t max_drain_batch = 256;
	size_t drain_batch = 64;
};

/// Per-loop observability counters. Only the loop thread mutates them.
struct event_loop_stats
{
	/// Counted offloaded operations (handle-based, e.g. start_resolve) started on this loop and not yet
	/// completed back on it. Before teardown, quiesce with \ref event_loop::settle.
	/// Plain \ref thread_pool::post offloads are not counted.
	uint64_t offload_in_flight = 0;

	/// Poller-plane I/O operations (handle-based, e.g. start_receive_from) started on this loop and not
	/// yet dispatched: outstanding with the backend (parked awaiting readiness, or held by the kernel), or
	/// awaiting dispatch from the ready queue. A completing op is excluded before its handler runs, so a handler
	/// observing zero is the operation that drained the loop. Before teardown, quiesce like \ref offload_in_flight:
	/// destroy the handles (cancelling their undispatched ops), then \ref event_loop::settle.
	uint64_t io_in_flight = 0;
};

namespace __event_loop
{

struct impl_type;
struct socket_state;

struct io_state
{
	static constexpr size_t endpoint_capacity = 32;

	alignas(std::max_align_t) std::array<std::byte, endpoint_capacity> endpoint;
	uint32_t endpoint_size;

	int native_error;
	size_t bytes;
	bool truncated;
	socket_state *owner;

	// Completion-plane per-op block, typed by the backend that uses it:
	// - OVERLAPPED and WSABUF in event_loop.iocp.cpp
	// - msghdr and iovec in event_loop.io_uring.cpp
	// - unused by reactor backends
	static constexpr size_t native_capacity = 72;
	alignas(std::max_align_t) std::array<std::byte, native_capacity> native;
};

[[nodiscard]] inline io_state &io (task &t) noexcept
{
	return t.scratch_as<io_state>();
}

struct socket_state
{
	struct direction
	{
		__task::attorney::task_queue pending{};
		bool ready = false;

		// Completion backends: ops armed with the kernel in this direction and not yet completed.
		// Bounds per-op arming (surplus stays in pending)
		size_t armed = 0;

		[[nodiscard]] bool actionable () const noexcept
		{
			return ready && !pending.empty();
		}
	};

	impl_type *const loop;
	const std::intptr_t handle;
	direction receive{}, send{};
	bool queued = false;
	intrusive_queue_hook<socket_state> hook{};

	void start_op (task_ptr &&t, direction &d) noexcept;

	[[nodiscard]] bool actionable () const noexcept
	{
		return receive.actionable() || send.actionable();
	}

	[[nodiscard]] size_t armed () const noexcept
	{
		return receive.armed + send.armed;
	}
};

using socket_queue = intrusive_queue<&socket_state::hook>;

struct socket_state_deleter
{
	void operator() (socket_state *s) const noexcept;
};

using socket_state_ptr = std::unique_ptr<socket_state, socket_state_deleter>;

struct impl_type
{
	using clock = std::chrono::steady_clock;

	clock::time_point now_ = now();
	const std::thread::id loop_thread_ = std::this_thread::get_id();
	task *timer_root_ = nullptr;
	std::atomic<size_t> posts_in_flight_ = 0;
	__task::attorney::task_queue posted_{};
	socket_queue actionable_{};
	__task::attorney::task_queue ready_{};
	event_loop_stats stats_{};
	event_loop_config config_{};

	void (*poll_fn)(impl_type &, clock::duration timeout) noexcept = nullptr;
	void (*drain_fn)(impl_type &, socket_state &, socket_state::direction &) noexcept = nullptr;

	virtual ~impl_type () noexcept = default;
	virtual void do_post (task &t) noexcept = 0;
	virtual std::error_code register_socket (socket_state &s) noexcept = 0;
	virtual void close_socket (socket_state &s) noexcept;

	size_t iterate (clock::duration timeout) noexcept;

	void post (task_ptr &&t) noexcept;
	size_t drain_posted () noexcept;
	void start_timer (task_ptr &&t, clock::time_point deadline) noexcept;
	size_t expire_timers () noexcept;
	void drain_actionable () noexcept;
	size_t dispatch_ready () noexcept;

	[[nodiscard]] static clock::time_point now () noexcept
	{
		return clock::now();
	}

	[[nodiscard]] bool has_counted_work () const noexcept
	{
		return stats_.io_in_flight != 0
			|| stats_.offload_in_flight != 0
			|| !posted_.empty()
			|| posts_in_flight_.load(std::memory_order_relaxed) != 0
		;
	}

	[[nodiscard]] bool has_work () const noexcept
	{
		return timer_root_ != nullptr || has_counted_work();
	}
};

using impl_ptr = std::unique_ptr<impl_type>;

struct op_post
{
	using signature = void(task_ptr &&) noexcept;

	template <typename F>
	static void dispatch (task &t, F &f) noexcept
	{
		f(t.borrow());
	}
};

inline void socket_state::start_op (task_ptr &&t, direction &d) noexcept
{
	io(*t).owner = this;
	d.pending.push(*t.release());
	if (d.ready && !queued)
	{
		queued = true;
		loop->actionable_.push(*this);
	}
	++loop->stats_.io_in_flight;
}

} // namespace __event_loop

/// Per-thread completion loop. Not thread-safe and unchecked: it is bound to the thread that called
/// \ref make_loop, and every member other than \ref post must be called on that thread.
class event_loop
{
public:

	using clock = std::chrono::steady_clock;

	explicit event_loop (__event_loop::impl_ptr impl) noexcept
		: impl_{std::move(impl)}
	{
	}

	event_loop (event_loop &&) noexcept = default;
	event_loop &operator= (event_loop &&) noexcept = default;
	~event_loop () noexcept = default;

	/// Monotonic time cached per run() iteration, updated after each poll, so per-operation reads avoid a
	/// clock syscall. The cached value is monotonic and has no wall-clock correspondence.
	[[nodiscard]] const clock::time_point &now () const noexcept
	{
		return impl_->now_;
	}

	/// Per-loop observability counters.
	[[nodiscard]] const event_loop_stats &stats () const noexcept
	{
		return impl_->stats_;
	}

	/// Run iterations until no work remains (immediate and delayed posts); returns the number of completions
	/// dispatched.
	result<size_t> run () noexcept;

	/// Poll without blocking: dispatch pending completions; returns the count (0 if none were ready).
	result<size_t> run_once () noexcept;

	/// Run one iteration, blocking at most \a timeout; returns the count. May return 0 before \a timeout
	/// elapses (e.g. on a wake whose work was already dispatched by an earlier iteration): 0 means nothing
	/// was dispatched this iteration, not that the full \a timeout was consumed.
	result<size_t> run_for (clock::duration timeout) noexcept;

	/// Run iterations until counted work drains -- posts in flight, poller-plane I/O and counted offloads;
	/// returns the number of completions dispatched. Armed timers neither count as work nor keep settle()
	/// alive; one expiring while settling is dispatched normally. The teardown quiesce step: destroy
	/// handles, settle(), destroy the loop, then the thread pool. Blocks as long as the slowest in-flight
	/// operation.
	result<size_t> settle () noexcept;

	/// Run the completion \a handler for \a t on this loop's thread (thread-safe).
	///
	/// \a handler is bound before the task is enqueued (the task is caller-owned, so no data race) and invoked from
	/// a subsequent run() on the loop thread.
	///
	/// Cross-thread posting may block under backpressure. Keep the destination loop progressing while
	/// producers are active; drain it before joining producers that may be blocked posting.
	template <typename H>
	void post (task_ptr &&t, H handler) noexcept
		requires __async::handler<H, void(task_ptr &&) noexcept>
	{
		t->bind<__event_loop::op_post>(std::move(handler));
		impl_->post(std::move(t));
	}

	/// Run the completion \a handler for \a t on this loop's thread once \a delay has elapsed, measured from
	/// \ref now (zero or negative: on the next iteration). Unlike \ref post, not thread-safe -- arm only from
	/// the loop thread. The delayed post occupies the task's op scratch while armed.
	///
	/// There is no cancellation: to move the due time, keep the authoritative deadline in app state and, when
	/// the handler fires early, re-arm for the remainder (lazy re-arm). A task still armed when the loop is
	/// destroyed is dropped without completing; its storage remains app property, but the task stays bound and
	/// must not be reused.
	template <typename H>
	void post_after (task_ptr &&t, clock::duration delay, H handler) noexcept
		requires __async::handler<H, void(task_ptr &&) noexcept>
	{
		t->bind<__event_loop::op_post>(std::move(handler));
		impl_->start_timer(std::move(t), impl_->now_ + delay);
	}

	/// Consume the configured synchronous \a resource, returning its async \ref handle bound to this
	/// loop, with offloaded work routed through \a pool. The handle binds heap-stable internals, so it
	/// survives moves of both this loop and \a pool; per the teardown contract it must be destroyed
	/// before either. Resource types with no backend setup step (e.g. resolver) cannot fail.
	/// Defined in pal/async/handle.hpp.
	template <typename T>
	[[nodiscard]] result<handle<T>> make_handle (T resource, thread_pool &pool) noexcept;

	/// \copydoc make_handle(T, thread_pool &)
	///
	/// Overload for poller-plane resources (e.g. datagram socket) that route no work through a
	/// thread_pool. Setup is the backend registration moment: sockets are switched to non-blocking and
	/// registered with the poller, either of which can fail.
	template <typename T>
	[[nodiscard]] result<handle<T>> make_handle (T resource) noexcept;

private:

	friend class thread_pool;

	__event_loop::impl_ptr impl_;
};

/// Create an event loop with the default backend for this platform, or an error if the platform has no
/// backend or a resource limit is hit. Returns std::errc::invalid_argument when \a config carries an
/// out-of-range batch bound.
///
/// The calling thread becomes the loop's thread. \ref event_loop::make_handle, \ref event_loop::post_after,
/// every start_* on a handle, handle destruction and driving the loop must run on it, as must moving and
/// destroying the loop object itself; \ref event_loop::post is the sole exception. Violations are undefined
/// behavior and unchecked.
result<event_loop> make_loop (const event_loop_config &config = {}) noexcept;

} // namespace pal::async
