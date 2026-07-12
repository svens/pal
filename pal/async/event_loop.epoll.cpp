#include <pal/version.hpp>

#if __pal_os_linux

#include <pal/async/event_loop.hpp>
#include <pal/error.hpp>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <new>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

namespace pal::async
{

namespace __event_loop
{

namespace
{

struct epoll_loop: impl_type
{
	int epoll = -1;
	int wake = -1;
	std::atomic<bool> signaled = false;

	~epoll_loop () noexcept
	{
		::close(wake);
		::close(epoll);
	}
};

constexpr ::timespec to_timespec (impl_type::clock::duration d) noexcept
{
	const auto wait = (d < impl_type::clock::duration::zero()) ? impl_type::clock::duration::zero() : d;
	const auto secs = std::chrono::duration_cast<std::chrono::seconds>(wait);
	const auto nsecs = std::chrono::duration_cast<std::chrono::nanoseconds>(wait - secs);
	return {
		.tv_sec = static_cast<::time_t>(secs.count()),
		.tv_nsec = static_cast<long>(nsecs.count()),
	};
}

void drain_wake_channel (epoll_loop &self) noexcept
{
	uint64_t counter{};
	std::ignore = ::read(self.wake, &counter, sizeof(counter));
	self.signaled.store(false, std::memory_order_release);
	self.stats_.wakeups++;
}

size_t epoll_poll (impl_type &base, impl_type::clock::duration timeout) noexcept
{
	auto &self = static_cast<epoll_loop &>(base);

	::timespec ts{};
	const ::timespec *tsp = nullptr;
	if (timeout != impl_type::clock::duration::max())
	{
		ts = to_timespec(timeout);
		tsp = &ts;
	}

	::epoll_event event{};
	int r = 0;
	do
	{
		r = ::epoll_pwait2(self.epoll, &event, 1, tsp, nullptr);
	} while (r < 0 && errno == EINTR);

	if (r > 0)
	{
		drain_wake_channel(self);
	}

	return 0;
}

impl_type::clock::time_point epoll_now (impl_type &) noexcept
{
	return impl_type::clock::now();
}

void epoll_wake (impl_type &base) noexcept
{
	auto &self = static_cast<epoll_loop &>(base);
	if (!self.signaled.exchange(true, std::memory_order_acq_rel))
	{
		const uint64_t one = 1;
		std::ignore = ::write(self.wake, &one, sizeof(one));
	}
}

void epoll_destroy (impl_type *base) noexcept
{
	delete static_cast<epoll_loop *>(base);
}

} // namespace

} // namespace __event_loop

result<event_loop> make_loop (const event_loop_config &config) noexcept
{
	using namespace __event_loop;

	const int epoll = ::epoll_create1(EPOLL_CLOEXEC);
	if (epoll == -1)
	{
		return unexpected{pal::this_thread::last_system_error()};
	}

	const int wake = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
	if (wake == -1)
	{
		const auto error = pal::this_thread::last_system_error();
		::close(epoll);
		return unexpected{error};
	}

	::epoll_event event{.events = EPOLLIN | EPOLLET, .data = {}};
	if (::epoll_ctl(epoll, EPOLL_CTL_ADD, wake, &event) == -1)
	{
		const auto error = pal::this_thread::last_system_error();
		::close(wake);
		::close(epoll);
		return unexpected{error};
	}

	auto *self = new (std::nothrow) epoll_loop{};
	if (self == nullptr)
	{
		::close(wake);
		::close(epoll);
		return make_unexpected(std::errc::not_enough_memory);
	}

	self->epoll = epoll;
	self->wake = wake;
	self->poll_fn = &epoll_poll;
	self->wake_fn = &epoll_wake;
	self->now_fn = &epoll_now;
	self->destroy_fn = &epoll_destroy;
	self->config_ = config;
	self->now_ = epoll_now(*self);

	return event_loop{impl_ptr{self}};
}

} // namespace pal::async

#endif // __pal_os_linux
