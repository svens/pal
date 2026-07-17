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

class thread_pool;

template <typename T>
class handle;

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

	/// Counted offloaded operations (handle-based, e.g. start_resolve) started on this loop and not yet
	/// completed back on it. Before teardown, quiesce by running the loop until this reaches zero.
	/// Plain \ref thread_pool::post offloads are not counted.
	uint64_t offload_in_flight = 0;

	/// Poller-plane I/O operations (handle-based, e.g. start_receive_from) started on this loop and not
	/// yet dispatched: parked in a resource's pending queues awaiting readiness, or awaiting dispatch
	/// from the ready queue. A completing op is excluded before its handler runs, so a handler observing zero is
	/// the operation that drained the loop. Before teardown, quiesce like \ref offload_in_flight: destroy the
	/// handles (cancelling their parked ops), then run the loop until this reaches zero.
	uint64_t io_in_flight = 0;
};

namespace __event_loop
{

struct impl_type;

struct io_state
{
	static constexpr size_t endpoint_capacity = 32;

	alignas(std::max_align_t) std::array<std::byte, endpoint_capacity> endpoint;
	uint32_t endpoint_size;

	std::error_code ec;
	size_t bytes;
	bool truncated;
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
	void (*drain_fn)(impl_type &, socket_state &) noexcept = nullptr;

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
		// clang-format off
		return inbox_.head() != nullptr
			|| timer_root_ != nullptr
			|| stats_.io_in_flight != 0
			|| stats_.offload_in_flight != 0;
		// clang-format on
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
void start_socket_op (task_ptr &&t, socket_state &s, socket_state::direction &d) noexcept;
void on_socket_event (socket_state &s, socket_state::direction &d) noexcept;
void on_wake (impl_type &l) noexcept;

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

	/// Monotonic time cached per run() iteration, so per-operation reads avoid a clock syscall. The backend owns
	/// the time source; the loop guarantees monotonic, cached time only, never correspondence to wall-clock
	/// progress.
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
	/// registered with the poller, either of which can fail; platforms whose backend has no socket
	/// machinery yet report \c operation_not_supported.
	template <typename T>
	[[nodiscard]] result<handle<T>> make_handle (T resource) noexcept;

private:

	explicit event_loop (__event_loop::impl_ptr impl) noexcept
		: impl_{std::move(impl)}
	{
	}

	friend result<event_loop> make_loop (const event_loop_config &) noexcept;
	friend class thread_pool;

	__event_loop::impl_ptr impl_;
};

/// Create an event loop with the default backend for this platform, or an error if the platform has no
/// backend or a resource limit is hit.
result<event_loop> make_loop (const event_loop_config &config = {}) noexcept;

} // namespace pal::async
