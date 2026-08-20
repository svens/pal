#include <pal/async/event_loop.hpp>
#include <pal/version.hpp>
#include <algorithm>
#include <system_error>
#include <utility>

#if __pal_os_linux || __pal_os_macos
	#include <cerrno>
#elif __pal_os_windows
	#include <winerror.h>
#endif

namespace pal::async
{

namespace __event_loop
{

namespace
{

constexpr size_t max_rounds_per_poll = 4;

// Armed timer's op state: https://en.wikipedia.org/wiki/Pairing_heap node. No decrease-key/removal: there
// is no cancellation, deadline moves use the lazy re-arm idiom.
struct timer_state
{
	impl_type::clock::time_point deadline;
	task *child;
	task *sibling;
};

timer_state &timer (task &t) noexcept
{
	return t.scratch_as<timer_state>();
}

task *meld (task *a, task *b) noexcept
{
	if (timer(*b).deadline < timer(*a).deadline)
	{
		std::swap(a, b);
	}
	timer(*b).sibling = timer(*a).child;
	timer(*a).child = b;
	return a;
}

// Standard pairing-heap two-pass merge of a popped root's child list; iterative, so heap size never costs
// stack depth.
task *merge_pairs (task *first) noexcept
{
	task *paired = nullptr;
	while (first != nullptr)
	{
		task *a = first;
		task *b = timer(*a).sibling;
		if (b == nullptr)
		{
			timer(*a).sibling = paired;
			paired = a;
			break;
		}
		first = timer(*b).sibling;
		task *m = meld(a, b);
		timer(*m).sibling = paired;
		paired = m;
	}

	task *root = nullptr;
	while (paired != nullptr)
	{
		task *next = timer(*paired).sibling;
		root = (root != nullptr) ? meld(root, paired) : paired;
		paired = next;
	}
	return root;
}

} // namespace

impl_type::~impl_type () noexcept
{
	pal_require(inbox_.head() == nullptr, "event_loop destroyed with a pending inbox");
	pal_require(stats_.offload_in_flight == 0, "event_loop destroyed with offloaded ops in flight");
	pal_require(stats_.io_in_flight == 0, "event_loop destroyed with I/O ops in flight");
}

size_t impl_type::drain_inbox () noexcept
{
	size_t n = 0;
	while (auto *t = inbox_.try_pop())
	{
		t->complete();
		++n;
	}
	return n;
}

size_t impl_type::expire_timers () noexcept
{
	size_t n = 0;
	while (timer_root_ != nullptr && timer(*timer_root_).deadline <= now_)
	{
		auto *t = timer_root_;
		timer_root_ = merge_pairs(timer(*t).child);
		t->complete();
		++n;
	}
	return n;
}

void impl_type::drain_actionable () noexcept
{
	// Terminates: each drain_fn visit either completes at least one pending op (finite supply -- no
	// handlers run here to add more) or clears the readiness flag of every direction it visited.
	while (auto *s = actionable_.try_pop())
	{
		s->queued = false;
		drain_fn(*this, *s);
		if (!s->queued && s->actionable())
		{
			// one fairness quantum consumed; round-robin to the tail
			s->queued = true;
			actionable_.push(*s);
		}
	}
}

size_t impl_type::dispatch_ready () noexcept
{
	size_t n = 0;
	while (auto *t = ready_.try_pop())
	{
		--stats_.io_in_flight;
		t->complete();
		++n;
	}
	return n;
}

size_t impl_type::iterate (clock::duration timeout) noexcept
{
	auto n = drain_inbox();
	if (n > 0 || !ready_.empty() || !actionable_.empty())
	{
		timeout = clock::duration::zero();
	}
	else if (timer_root_ != nullptr)
	{
		now_ = now_fn(*this);
		const auto &deadline = timer(*timer_root_).deadline;
		timeout = (std::min)(timeout, deadline > now_ ? deadline - now_ : clock::duration::zero());
	}

	poll_fn(*this, timeout);

	now_ = now_fn(*this);

	n += drain_inbox();
	n += expire_timers();

	for (auto round = max_rounds_per_poll; round != 0; --round)
	{
		drain_actionable();
		const auto dispatched = dispatch_ready();
		if (dispatched == 0)
		{
			break;
		}
		n += dispatched;
	}

	stats_.completions += n;
	return n;
}

void post (impl_type &l, task_ptr &&t) noexcept
{
	auto *raw = t.release();
	l.inbox_.push(*raw);
	if (!l.signaled_.exchange(true, std::memory_order_acq_rel))
	{
		l.wake_fn(l);
	}
}

void on_wake (impl_type &l) noexcept
{
	l.signaled_.store(false, std::memory_order_release);
	l.stats_.wakeups++;
}

void start_timer (impl_type &l, task_ptr &&t, impl_type::clock::time_point deadline) noexcept
{
	auto *raw = t.release();
	timer(*raw) = {.deadline = deadline, .child = nullptr, .sibling = nullptr};
	l.timer_root_ = (l.timer_root_ != nullptr) ? meld(l.timer_root_, raw) : raw;
}

void submit_socket_op (impl_type &l, task &t, socket_state &s, socket_state::direction &d) noexcept
{
	d.pending.push(t);
	if (d.ready && !s.queued)
	{
		s.queued = true;
		l.actionable_.push(s);
	}
}

void on_socket_event (socket_state &s, socket_state::direction &d) noexcept
{
	d.ready = true;
	if (!d.pending.empty() && !s.queued)
	{
		s.queued = true;
		s.loop->actionable_.push(s);
	}
}

void close_socket (impl_type &l, socket_state &s) noexcept
{
	#if __pal_os_windows
		constexpr auto cancel_error = ERROR_OPERATION_ABORTED;
	#else
		constexpr auto cancel_error = ECANCELED;
	#endif

	l.ready_.splice(std::move(s.receive.pending));
	l.ready_.splice(std::move(s.send.pending));

	for (auto &t: l.ready_)
	{
		if (io(t).owner == &s)
		{
			io(t).native_error = cancel_error;
		}
	}

	if (s.queued)
	{
		l.actionable_.remove(s);
	}

	delete &s;
}

void socket_state_deleter::operator() (socket_state *s) const noexcept
{
	s->loop->close_socket_fn(*s->loop, *s);
}

event_loop wrap (impl_ptr impl) noexcept
{
	return event_loop{std::move(impl)};
}

template <bool inject_probe_failure>
result<event_loop> make_loop_impl (const event_loop_config &config) noexcept
{
	if (!is_valid(config))
	{
		return make_unexpected(std::errc::invalid_argument);
	}

	result<impl_ptr> impl = make_unexpected(std::errc::not_supported);

	if constexpr (os == os_type::linux)
	{
		if constexpr (!inject_probe_failure)
		{
			impl = make_io_uring_loop(config);
		}

		if (!impl)
		{
			impl = make_epoll_loop(config);
		}
	}
	else if constexpr (os == os_type::macos)
	{
		impl = make_kqueue_loop(config);
	}
	else if constexpr (os == os_type::windows)
	{
		impl = make_iocp_loop(config);
	}

	return std::move(impl).transform(wrap);
}

template result<event_loop> make_loop_impl<false> (const event_loop_config &) noexcept;
template result<event_loop> make_loop_impl<true> (const event_loop_config &) noexcept;

} // namespace __event_loop

result<event_loop> make_loop (const event_loop_config &config) noexcept
{
	return __event_loop::make_loop_impl<false>(config);
}

result<size_t> event_loop::run () noexcept
{
	size_t total = 0;
	while (impl_->has_work())
	{
		total += impl_->iterate((clock::duration::max)());
	}
	return total;
}

result<size_t> event_loop::run_once () noexcept
{
	return impl_->iterate(clock::duration::zero());
}

result<size_t> event_loop::run_for (clock::duration timeout) noexcept
{
	return impl_->iterate(timeout);
}

} // namespace pal::async
