#pragma once

/**
 * \file pal/async/event_loop.hpp
 * Per-thread completion loop: a portable run() layer over a backend poll()/wake() primitive
 */

#include <pal/async/task.hpp>
#include <pal/intrusive_queue.hpp>
#include <pal/result.hpp>
#include <array>
#include <atomic>
#include <chrono>
#include <memory>

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
	/// Completions dispatched from run()
	uint64_t completions = 0;

	/// Backend wake() deliveries observed by poll()
	uint64_t wakeups = 0;

	/// Counted offloaded operations (handle-based, e.g. start_resolve) started on this loop and not yet
	/// completed back on it. Before teardown, quiesce by running the loop until this reaches zero.
	/// Plain \ref thread_pool::post offloads are not counted.
	uint64_t offload_in_flight = 0;

	/// Poller-plane I/O operations (handle-based, e.g. start_receive_from) started on this loop and not
	/// yet dispatched: outstanding with the backend (parked awaiting readiness, or held by the kernel), or
	/// awaiting dispatch from the ready queue. A completing op is excluded before its handler runs, so a handler
	/// observing zero is the operation that drained the loop. Before teardown, quiesce like \ref offload_in_flight:
	/// destroy the handles (cancelling their undispatched ops), then run the loop until this reaches zero.
	uint64_t io_in_flight = 0;
};

namespace __event_loop
{

[[nodiscard]] constexpr bool is_valid (const event_loop_config &config) noexcept
{
	return config.poll_batch >= 1
		&& config.poll_batch <= event_loop_config::max_poll_batch
		&& config.drain_batch >= 1
		&& config.drain_batch <= event_loop_config::max_drain_batch
		&& config.submission_depth >= 1
		&& config.submission_depth <= event_loop_config::max_submission_depth
		&& config.completion_depth >= config.submission_depth
		&& config.completion_depth <= event_loop_config::max_completion_depth
	;
}

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

		// Completion backends that arm each op with the kernel individually: how many are armed now, so a
		// backend whose per-op cost grows with the count can bound it and leave the surplus in pending.
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
	size_t native_in_flight = 0;
	intrusive_queue_hook<socket_state> hook{};

	[[nodiscard]] bool actionable () const noexcept
	{
		return receive.actionable() || send.actionable();
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

	void (*poll_fn)(impl_type &, clock::duration timeout) noexcept = nullptr;
	void (*wake_fn)(impl_type &) noexcept = nullptr;
	clock::time_point (*now_fn)(impl_type &) noexcept = nullptr;
	void (*destroy_fn)(impl_type *) noexcept = nullptr;

	std::error_code (*register_socket_fn)(impl_type &, socket_state &) noexcept = nullptr;
	void (*submit_socket_op_fn)(impl_type &, task &, socket_state &, socket_state::direction &) noexcept = nullptr;
	void (*drain_fn)(impl_type &, socket_state &) noexcept = nullptr;
	void (*close_socket_fn)(impl_type &, socket_state &) noexcept = nullptr;

	clock::time_point now_{};
	std::atomic<bool> signaled_ = false;
	task *timer_root_ = nullptr;
	__task::attorney::task_mpsc_queue inbox_{};
	socket_queue actionable_{};
	__task::attorney::task_queue ready_{};
	event_loop_stats stats_{};
	event_loop_config config_{};

	~impl_type () noexcept;

	[[nodiscard]] bool has_work () const noexcept
	{
		return inbox_.head() != nullptr
			|| timer_root_ != nullptr
			|| stats_.io_in_flight != 0
			|| stats_.offload_in_flight != 0
		;
	}

	size_t iterate (clock::duration timeout) noexcept;
	size_t drain_inbox () noexcept;
	size_t expire_timers () noexcept;
	void drain_actionable () noexcept;
	size_t dispatch_ready () noexcept;
};

struct deleter
{
	void operator() (impl_type *l) const noexcept
	{
		l->destroy_fn(l);
	}
};

using impl_ptr = std::unique_ptr<impl_type, deleter>;

struct op_post
{
	using signature = void(task_ptr &&) noexcept;

	template <typename F>
	static void dispatch (task &t, F &f) noexcept
	{
		f(t.borrow());
	}
};

void post (impl_type &l, task_ptr &&t) noexcept;
void start_timer (impl_type &l, task_ptr &&t, impl_type::clock::time_point deadline) noexcept;
void submit_socket_op (impl_type &l, task &t, socket_state &s, socket_state::direction &d) noexcept;
void close_socket (impl_type &l, socket_state &s) noexcept;
void on_socket_event (socket_state &s, socket_state::direction &d) noexcept;
void on_wake (impl_type &l) noexcept;

inline void start_socket_op (task_ptr &&t, socket_state &s, socket_state::direction &d) noexcept
{
	++s.loop->stats_.io_in_flight;
	io(*t).owner = &s;
	s.loop->submit_socket_op_fn(*s.loop, *t.release(), s, d);
}

event_loop wrap (impl_ptr impl) noexcept;
result<impl_ptr> make_epoll_loop (const event_loop_config &config) noexcept;
result<impl_ptr> make_io_uring_loop (const event_loop_config &config) noexcept;
result<impl_ptr> make_kqueue_loop (const event_loop_config &config) noexcept;
result<impl_ptr> make_iocp_loop (const event_loop_config &config) noexcept;

// make_loop's body, with the io_uring probe replaceable by a compile-time failure so the fallback to
// epoll is a tested path on machines where io_uring works.
template <bool inject_probe_failure>
result<event_loop> make_loop_impl (const event_loop_config &config) noexcept;

extern template result<event_loop> make_loop_impl<false> (const event_loop_config &) noexcept;
extern template result<event_loop> make_loop_impl<true> (const event_loop_config &) noexcept;

} // namespace __event_loop

/// Per-thread completion loop. Not thread-safe and unchecked: it is bound to the thread that called
/// \ref make_loop, and every member other than \ref post must be called on that thread.
class event_loop
{
public:

	using clock = std::chrono::steady_clock;

	event_loop (event_loop &&) noexcept = default;
	event_loop &operator= (event_loop &&) noexcept = default;
	~event_loop () noexcept = default;

	/// Monotonic time cached per run() iteration, updated after each poll, so per-operation reads avoid a
	/// clock syscall. The backend owns the time source; the loop guarantees monotonic, cached time only, never
	/// correspondence to wall-clock progress.
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
		__event_loop::start_timer(*impl_, std::move(t), impl_->now_ + delay);
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

	explicit event_loop (__event_loop::impl_ptr impl) noexcept
		: impl_{std::move(impl)}
	{
	}

	friend event_loop __event_loop::wrap (__event_loop::impl_ptr) noexcept;
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
