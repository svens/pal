#include <pal/async/event_loop>

namespace pal::async {

void event_loop::run (std::chrono::milliseconds duration) noexcept
{
	if (!impl_->completed.empty())
	{
		now_ = clock_type::now();
		notify_completions();
		duration = std::chrono::milliseconds::zero();
	}

	impl_->poll(duration);

	now_ = clock_type::now();
	notify_completions();
}

void event_loop::notify_completions () noexcept
{
	auto tasks = std::move(impl_->completed);
	while (auto task = tasks.try_pop())
	{
		task->completed();
	}
}

} // namespace pal::async
