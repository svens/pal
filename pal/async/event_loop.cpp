#include <pal/async/event_loop.hpp>

namespace pal::async
{

namespace __event_loop
{

impl_type::~impl_type () noexcept
{
	pal_require(inbox_.empty(), "event_loop destroyed with a pending inbox");
	pal_require(pending_.load(std::memory_order_acquire) == 0, "event_loop destroyed with work in flight");
	pal_require(stats_.offload_in_flight == 0, "event_loop destroyed with offload in flight");
}

size_t impl_type::drain_inbox () noexcept
{
	size_t n = 0;
	while (task *t = inbox_.try_pop())
	{
		t->complete({}, 0);
		pending_.fetch_sub(1, std::memory_order_release);
		++n;
	}
	return n;
}

size_t impl_type::iterate (clock::duration timeout) noexcept
{
	now_ = clock::now();

	auto n = drain_inbox();
	if (n > 0)
	{
		timeout = clock::duration::zero();
	}

	n += poll_fn(*this, timeout);

	n += drain_inbox();

	stats_.completions += n;
	return n;
}

void post (impl_type &l, task_ptr &&t) noexcept
{
	task *raw = t.release();
	l.pending_.fetch_add(1, std::memory_order_relaxed);
	l.inbox_.push(*raw);
	l.wake_fn(l);
}

} // namespace __event_loop

result<size_t> event_loop::run () noexcept
{
	size_t total = 0;
	while (impl_->pending_.load(std::memory_order_acquire) > 0)
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
