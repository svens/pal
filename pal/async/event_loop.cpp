#include <pal/async/event_loop.hpp>
#include <algorithm>
#include <utility>

namespace pal::async
{

namespace __event_loop
{

namespace
{

/// Armed timer's op state: https://en.wikipedia.org/wiki/Pairing_heap node. No decrease-key/removal: there
/// is no cancellation, deadline moves use the lazy re-arm idiom.
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

/// Standard pairing-heap two-pass merge of a popped root's child list; iterative, so heap size never costs
/// stack depth.
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
}

size_t impl_type::drain_inbox () noexcept
{
	size_t n = 0;
	while (task *t = inbox_.try_pop())
	{
		t->complete({}, 0);
		++n;
	}
	return n;
}

size_t impl_type::expire_timers () noexcept
{
	size_t n = 0;
	while (timer_root_ != nullptr && timer(*timer_root_).deadline <= now_)
	{
		// pop before complete(): the handler may re-arm this same task
		auto *t = timer_root_;
		timer_root_ = merge_pairs(timer(*t).child);
		t->complete({}, 0);
		++n;
	}
	return n;
}

size_t impl_type::iterate (clock::duration timeout) noexcept
{
	now_ = now_fn(*this);

	auto n = drain_inbox();
	if (n > 0)
	{
		timeout = clock::duration::zero();
	}
	else if (timer_root_ != nullptr)
	{
		const auto &deadline = timer(*timer_root_).deadline;
		timeout = std::min(timeout, deadline > now_ ? deadline - now_ : clock::duration::zero());
	}

	n += poll_fn(*this, timeout);

	now_ = now_fn(*this);

	n += drain_inbox();
	n += expire_timers();

	stats_.completions += n;
	return n;
}

void post (impl_type &l, task_ptr &&t) noexcept
{
	task *raw = t.release();
	l.inbox_.push(*raw);
	l.wake_fn(l);
}

void start_timer (impl_type &l, task_ptr &&t, impl_type::clock::time_point deadline) noexcept
{
	task *raw = t.release();
	timer(*raw) = {.deadline = deadline, .child = nullptr, .sibling = nullptr};
	l.timer_root_ = (l.timer_root_ != nullptr) ? meld(l.timer_root_, raw) : raw;
}

} // namespace __event_loop

result<size_t> event_loop::run () noexcept
{
	size_t total = 0;
	while (impl_->inbox_.head() != nullptr || impl_->timer_root_ != nullptr)
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
