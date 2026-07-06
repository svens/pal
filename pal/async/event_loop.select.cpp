#include <pal/async/event_loop.hpp>
#include <pal/error.hpp>
#include <pal/version.hpp>

#if __pal_os_windows

// select() on Windows operates only on winsock SOCKETs, so the self-pipe wake primitive does not port here;
// this platform has no loop backend.
namespace pal::async
{

result<event_loop> make_loop (const event_loop_config &) noexcept
{
	return make_unexpected(std::errc::not_supported);
}

} // namespace pal::async

#else

#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <fcntl.h>
#include <new>
#include <sys/select.h>
#include <unistd.h>

namespace pal::async
{

namespace __event_loop
{

namespace
{

/// select/self-pipe loop backend. poll() waits on the wake pipe and dispatches no completions of its own; the
/// pipe exists so a cross-thread wake() can unblock a waiting poll(). \c signaled coalesces wakes: a producer
/// writes a pipe byte only on the false->true transition, so steady traffic pays at most one syscall per
/// drained batch.
struct select_loop: impl_type
{
	int wake_r = -1;
	int wake_w = -1;
	std::atomic<bool> signaled = false;

	~select_loop () noexcept
	{
		if (wake_r != -1)
		{
			::close(wake_r);
		}
		if (wake_w != -1)
		{
			::close(wake_w);
		}
	}
};

constexpr ::timeval to_timeval (impl_type::clock::duration d) noexcept
{
	const auto wait = (d < impl_type::clock::duration::zero()) ? impl_type::clock::duration::zero() : d;
	const auto secs = std::chrono::duration_cast<std::chrono::seconds>(wait);
	const auto usecs = std::chrono::duration_cast<std::chrono::microseconds>(wait - secs);
	return {
		.tv_sec = static_cast<::time_t>(secs.count()),
		.tv_usec = static_cast<::suseconds_t>(usecs.count()),
	};
}

void drain_wake_channel (select_loop &self) noexcept
{
	constexpr std::size_t capacity = 64;
	std::array<std::byte, capacity> buffer;

	while (::read(self.wake_r, buffer.data(), buffer.size()) > 0)
	{
	}

	self.signaled.store(false, std::memory_order_release);
	self.stats_.wakeups++;
}

size_t select_poll (impl_type &base, impl_type::clock::duration timeout) noexcept
{
	auto &self = static_cast<select_loop &>(base);

	fd_set readable;
	FD_ZERO(&readable);
	FD_SET(self.wake_r, &readable);

	::timeval tv{};
	::timeval *tvp = nullptr;
	if (timeout != impl_type::clock::duration::max())
	{
		tv = to_timeval(timeout);
		tvp = &tv;
	}

	int r = 0;
	do
	{
		r = ::select(self.wake_r + 1, &readable, nullptr, nullptr, tvp);
	} while (r < 0 && errno == EINTR);

	if (r > 0 && FD_ISSET(self.wake_r, &readable))
	{
		drain_wake_channel(self);
	}

	return 0;
}

void select_wake (impl_type &base) noexcept
{
	auto &self = static_cast<select_loop &>(base);
	if (!self.signaled.exchange(true, std::memory_order_acq_rel))
	{
		const std::byte one{1};
		std::ignore = ::write(self.wake_w, &one, sizeof(one));
	}
}

void select_destroy (impl_type *base) noexcept
{
	delete static_cast<select_loop *>(base);
}

} // namespace

} // namespace __event_loop

result<event_loop> make_loop (const event_loop_config &config) noexcept
{
	using namespace __event_loop;

	std::array<int, 2> fds{};
	if (::pipe(fds.data()) != 0)
	{
		return unexpected{pal::this_thread::last_system_error()};
	}
	for (const int fd: fds)
	{
		::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
		::fcntl(fd, F_SETFD, ::fcntl(fd, F_GETFD, 0) | FD_CLOEXEC);
	}

	auto *self = new (std::nothrow) select_loop{};
	if (self == nullptr)
	{
		::close(fds[0]);
		::close(fds[1]);
		return make_unexpected(std::errc::not_enough_memory);
	}

	self->wake_r = fds[0];
	self->wake_w = fds[1];
	self->poll_fn = &select_poll;
	self->wake_fn = &select_wake;
	self->destroy_fn = &select_destroy;
	self->config_ = config;

	return event_loop{impl_ptr{self}};
}

} // namespace pal::async

#endif // __pal_os_windows
