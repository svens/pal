#include <pal/async/event_loop.hpp>
#include <algorithm>
#include <utility>

namespace pal::async
{

namespace __event_loop
{

namespace
{

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
		if (s->actionable())
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
	now_ = now_fn(*this);

	auto n = drain_inbox();
	if (n > 0 || !ready_.empty() || !actionable_.empty())
	{
		timeout = clock::duration::zero();
	}
	else if (timer_root_ != nullptr)
	{
		const auto &deadline = timer(*timer_root_).deadline;
		timeout = std::min(timeout, deadline > now_ ? deadline - now_ : clock::duration::zero());
	}

	poll_fn(*this, timeout);

	now_ = now_fn(*this);

	n += drain_inbox();
	n += expire_timers();

	// dispatch first: handler-issued ops drain same iteration
	n += dispatch_ready();
	drain_actionable();

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

void start_socket_op (task_ptr &&t, socket_state &s, socket_state::direction &d) noexcept
{
	++s.loop->stats_.io_in_flight;
	d.pending.push(*t.release());
	if (d.ready && !s.queued)
	{
		s.queued = true;
		s.loop->actionable_.push(s);
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

void socket_state_deleter::operator() (socket_state *s) const noexcept
{
	for (auto *d: {&s->receive, &s->send})
	{
		while (auto *t = d->pending.try_pop())
		{
			io(*t).ec = std::make_error_code(std::errc::operation_canceled);
			s->loop->ready_.push(*t);
		}
	}

	if (s->queued)
	{
		s->loop->actionable_.remove(*s);
	}

	delete s;
}

} // namespace __event_loop

result<size_t> event_loop::run () noexcept
{
	size_t total = 0;
	while (impl_->has_work())
	{
		total += impl_->iterate(clock::duration::max());
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
