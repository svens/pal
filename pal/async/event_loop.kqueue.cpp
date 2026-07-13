#include <pal/version.hpp>

#if __pal_os_macos

#include <pal/async/event_loop.hpp>
#include <pal/error.hpp>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <fcntl.h>
#include <new>
#include <sys/event.h>
#include <unistd.h>

namespace pal::async
{

namespace __event_loop
{

namespace
{

struct kqueue_loop: impl_type
{
	int kq = -1;
	std::atomic<bool> signaled = false;

	~kqueue_loop () noexcept
	{
		::close(kq);
	}
};

constexpr uintptr_t wake_ident = 1;

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

size_t kqueue_poll (impl_type &base, impl_type::clock::duration timeout) noexcept
{
	auto &self = static_cast<kqueue_loop &>(base);

	::timespec ts{};
	const ::timespec *tsp = nullptr;
	if (timeout != impl_type::clock::duration::max())
	{
		ts = to_timespec(timeout);
		tsp = &ts;
	}

	struct ::kevent event{};
	int r = 0;
	do
	{
		r = ::kevent(self.kq, nullptr, 0, &event, 1, tsp);
	} while (r < 0 && errno == EINTR);

	if (r > 0)
	{
		self.signaled.store(false, std::memory_order_release);
		self.stats_.wakeups++;
	}

	return 0;
}

impl_type::clock::time_point kqueue_now (impl_type &) noexcept
{
	return impl_type::clock::now();
}

void kqueue_wake (impl_type &base) noexcept
{
	auto &self = static_cast<kqueue_loop &>(base);
	if (!self.signaled.exchange(true, std::memory_order_acq_rel))
	{
		struct ::kevent trigger{};
		EV_SET(&trigger, wake_ident, EVFILT_USER, 0, NOTE_TRIGGER, 0, nullptr);
		std::ignore = ::kevent(self.kq, &trigger, 1, nullptr, 0, nullptr);
	}
}

void kqueue_destroy (impl_type *base) noexcept
{
	delete static_cast<kqueue_loop *>(base);
}

} // namespace

} // namespace __event_loop

result<event_loop> make_loop (const event_loop_config &config) noexcept
{
	using namespace __event_loop;

	const int kq = ::kqueue();
	if (kq == -1)
	{
		return unexpected{pal::this_thread::last_system_error()};
	}
	::fcntl(kq, F_SETFD, ::fcntl(kq, F_GETFD, 0) | FD_CLOEXEC);

	struct ::kevent reg{};
	EV_SET(&reg, wake_ident, EVFILT_USER, EV_ADD | EV_CLEAR, 0, 0, nullptr);
	if (::kevent(kq, &reg, 1, nullptr, 0, nullptr) == -1)
	{
		const auto error = pal::this_thread::last_system_error();
		::close(kq);
		return unexpected{error};
	}

	auto *self = new (std::nothrow) kqueue_loop{};
	if (self == nullptr)
	{
		::close(kq);
		return make_unexpected(std::errc::not_enough_memory);
	}

	self->kq = kq;
	self->poll_fn = &kqueue_poll;
	self->wake_fn = &kqueue_wake;
	self->now_fn = &kqueue_now;
	self->destroy_fn = &kqueue_destroy;
	self->config_ = config;
	self->now_ = kqueue_now(*self);

	return event_loop{impl_ptr{self}};
}

} // namespace pal::async

#endif // __pal_os_macos
