#include <pal/async/__event_loop.hpp>
#include <pal/version.hpp>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <new>
#include <system_error>
#include <thread>
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

[[nodiscard]] constexpr bool is_valid (const event_loop_config &config) noexcept
{
	return config.poll_batch >= 1
		&& config.poll_batch <= event_loop_config::max_poll_batch
		&& config.drain_batch >= 1
		&& config.drain_batch <= event_loop_config::max_drain_batch
	;
}

clock::time_point system_now (void *) noexcept
{
	return clock::now();
}

clock::time_point read_now (const impl_type &loop) noexcept
{
	return loop.config_.now(loop.config_.now_context);
}

// Armed timer's op state: https://en.wikipedia.org/wiki/Pairing_heap node. No decrease-key/removal: there
// is no cancellation, deadline moves use the lazy re-arm idiom.
struct timer_state
{
	clock::time_point deadline;
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

void expire_timers (impl_type &loop) noexcept
{
	while (loop.timer_root_ != nullptr && timer(*loop.timer_root_).deadline <= loop.now_)
	{
		auto *t = loop.timer_root_;
		loop.timer_root_ = merge_pairs(timer(*t).child);
		loop.posted_.push(*t);
	}
}

size_t drain_posted (impl_type &loop) noexcept
{
	size_t n = 0;
	auto batch = std::move(loop.posted_);
	while (auto *t = batch.try_pop())
	{
		t->complete();
		++n;
	}
	return n;
}

void drain_actionable (impl_type &loop) noexcept
{
	// Terminates: each drain visit either completes at least one pending op (finite supply -- no
	// handlers run here to add more) or clears the readiness flag of every direction it visited.
	while (auto *s = loop.actionable_.try_pop())
	{
		s->queued = false;
		for (auto *d: {&s->receive, &s->send})
		{
			if (d->actionable())
			{
				loop.drain(*s, *d);
			}
		}
		if (!s->queued && s->actionable())
		{
			// one fairness quantum consumed; round-robin to the tail
			s->queued = true;
			loop.actionable_.push(*s);
		}
	}
}

size_t dispatch_ready (impl_type &loop) noexcept
{
	size_t n = 0;
	while (auto *t = loop.ready_.try_pop())
	{
		--loop.stats_.io_in_flight;
		t->complete();
		++n;
	}
	return n;
}

size_t iterate (impl_type &loop, clock::duration timeout) noexcept
{
	if (!loop.posted_.empty() || !loop.ready_.empty() || !loop.actionable_.empty())
	{
		timeout = clock::duration::zero();
	}
	else if (loop.timer_root_ != nullptr)
	{
		loop.now_ = read_now(loop);
		const auto &deadline = timer(*loop.timer_root_).deadline;
		timeout = (std::min)(timeout, deadline > loop.now_ ? deadline - loop.now_ : clock::duration::zero());
	}

	loop.poll(timeout);

	loop.now_ = read_now(loop);

	expire_timers(loop);
	auto n = drain_posted(loop);

	for (auto round = max_rounds_per_poll; round != 0; --round)
	{
		drain_actionable(loop);
		const auto dispatched = dispatch_ready(loop);
		if (dispatched == 0)
		{
			break;
		}
		n += dispatched;
	}

	return n;
}

[[nodiscard]] bool has_counted_work (const impl_type &loop) noexcept
{
	return loop.stats_.io_in_flight != 0
		|| loop.stats_.offload_in_flight != 0
		|| !loop.posted_.empty()
		|| loop.posts_in_flight_.load(std::memory_order_relaxed) != 0
	;
}

[[nodiscard]] bool has_work (const impl_type &loop) noexcept
{
	return loop.timer_root_ != nullptr || has_counted_work(loop);
}

} // namespace

void post (impl_type &loop, task_ptr &&t) noexcept
{
	auto *raw = t.release();
	if (std::this_thread::get_id() == loop.loop_thread_)
	{
		loop.posted_.push(*raw);
		return;
	}
	loop.posts_in_flight_.fetch_add(1, std::memory_order_relaxed);
	loop.post(*raw);
}

void post_after (impl_type &loop, task_ptr &&t, clock::duration delay) noexcept
{
	auto *raw = t.release();
	timer(*raw) = {.deadline = loop.now_ + delay, .child = nullptr, .sibling = nullptr};
	loop.timer_root_ = (loop.timer_root_ != nullptr) ? meld(loop.timer_root_, raw) : raw;
}

void impl_deleter::operator() (impl_type *loop) const noexcept
{
	delete loop;
}

result<socket_state_ptr> make_socket_state (impl_type &loop, std::intptr_t handle) noexcept
{
	socket_state_ptr state{new (std::nothrow) socket_state{.loop = &loop, .handle = handle}};
	if (state == nullptr)
	{
		return make_unexpected(std::errc::not_enough_memory);
	}

	if (auto error = loop.register_socket(*state))
	{
		return unexpected{error};
	}

	return state;
}

void socket_state_deleter::operator() (socket_state *s) const noexcept
{
	#if __pal_os_windows
		constexpr auto cancel_error = ERROR_OPERATION_ABORTED;
	#else
		constexpr auto cancel_error = ECANCELED;
	#endif

	auto &loop = *s->loop;
	loop.quiesce(*s);

	loop.ready_.splice(std::move(s->receive.pending));
	loop.ready_.splice(std::move(s->send.pending));

	for (auto &t: loop.ready_)
	{
		if (io(t).owner == s)
		{
			io(t).native_error = cancel_error;
		}
	}

	if (s->queued)
	{
		loop.actionable_.remove(*s);
	}

	delete s;
}

namespace
{

void start_op (socket_state &s, task_ptr &&t, socket_state::direction &d) noexcept
{
	auto &op = io(*t);
	op.owner = &s;
	op.native_error = 0;
	d.pending.push(*t.release());
	if (d.ready && !s.queued)
	{
		s.queued = true;
		s.loop->actionable_.push(s);
	}
	++s.loop->stats_.io_in_flight;
}

} // namespace

void start_receive (socket_state &s, task_ptr &&t) noexcept
{
	start_op(s, std::move(t), s.receive);
}

void start_send (socket_state &s, task_ptr &&t) noexcept
{
	start_op(s, std::move(t), s.send);
}

} // namespace __event_loop

result<event_loop> make_loop (const event_loop_config &config) noexcept
{
	using namespace __event_loop;

	if (!is_valid(config))
	{
		return make_unexpected(std::errc::invalid_argument);
	}

	auto effective = config;
	if (effective.now == nullptr)
	{
		effective.now = &system_now;
	}

	return make_backend(effective).transform([](impl_ptr impl) noexcept
	{
		impl->now_ = read_now(*impl);
		return event_loop{std::move(impl)};
	});
}

const event_loop::clock::time_point &event_loop::now () const noexcept
{
	return impl_->now_;
}

const event_loop_stats &event_loop::stats () const noexcept
{
	return impl_->stats_;
}

result<size_t> event_loop::run () noexcept
{
	size_t total = 0;
	while (__event_loop::has_work(*impl_))
	{
		total += __event_loop::iterate(*impl_, (clock::duration::max)());
	}
	return total;
}

result<size_t> event_loop::run_once () noexcept
{
	return __event_loop::iterate(*impl_, clock::duration::zero());
}

result<size_t> event_loop::run_for (clock::duration timeout) noexcept
{
	return __event_loop::iterate(*impl_, timeout);
}

result<size_t> event_loop::settle () noexcept
{
	size_t total = 0;
	while (__event_loop::has_counted_work(*impl_))
	{
		total += __event_loop::iterate(*impl_, (clock::duration::max)());
	}
	return total;
}

} // namespace pal::async
