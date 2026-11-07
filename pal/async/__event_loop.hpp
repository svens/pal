#pragma once

/**
 * \file pal/async/__event_loop.hpp
 * Cross-TU internal seam for pal/async/event_loop backends (internal)
 */

#include <pal/async/event_loop.hpp>
#include <pal/intrusive_queue.hpp>
#include <pal/version.hpp>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <system_error>
#include <thread>
#include <tuple>
#include <utility>

#if __pal_os_linux || __pal_os_macos
	#include <pal/error.hpp>
	#include <bit>
	#include <cerrno>
	#include <chrono>
	#include <fcntl.h>
	#include <time.h>
	#include <unistd.h>
#endif

#if __pal_os_linux
	#include <sys/epoll.h>
	#include <sys/socket.h>
#elif __pal_os_macos
	#include <sys/event.h>
#elif __pal_os_windows
	#include <winsock2.h>
	#include <windows.h>
#endif

namespace pal::async::__event_loop
{

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
	size_t armed = 0;
	intrusive_queue_hook<socket_state> hook{};

	[[nodiscard]] bool actionable () const noexcept
	{
		return receive.actionable() || send.actionable();
	}
};

using socket_queue = intrusive_queue<&socket_state::hook>;

template <typename T, auto Close, T Invalid>
class unique_handle
{
public:

	unique_handle () = default;

	explicit unique_handle (T value) noexcept
		: value_{value}
	{
	}

	unique_handle (unique_handle &&that) noexcept
		: value_{std::exchange(that.value_, Invalid)}
	{
	}

	unique_handle &operator= (unique_handle &&that) noexcept
	{
		std::swap(value_, that.value_);
		return *this;
	}

	unique_handle (const unique_handle &) = delete;
	unique_handle &operator= (const unique_handle &) = delete;

	~unique_handle () noexcept
	{
		if (value_ != Invalid)
		{
			std::ignore = Close(value_);
		}
	}

	operator T () const noexcept
	{
		return value_;
	}

	explicit operator bool () const noexcept
	{
		return value_ != Invalid;
	}

private:

	T value_ = Invalid;
};

#if __pal_os_linux || __pal_os_macos

using unique_fd = unique_handle<int, ::close, -1>;

struct wake_channel
{
	unique_fd read{};
	unique_fd write{};

	static result<wake_channel> make () noexcept
	{
		int fd[2];
		#if __pal_os_linux
		{
			if (::pipe2(fd, O_CLOEXEC) == -1)
			{
				return unexpected{pal::this_thread::last_system_error()};
			}
		}
		#else
		{
			if (::pipe(fd) == -1)
			{
				return unexpected{pal::this_thread::last_system_error()};
			}
			::fcntl(fd[0], F_SETFD, ::fcntl(fd[0], F_GETFD, 0) | FD_CLOEXEC);
			::fcntl(fd[1], F_SETFD, ::fcntl(fd[1], F_GETFD, 0) | FD_CLOEXEC);
		}
		#endif
		::fcntl(fd[0], F_SETFL, ::fcntl(fd[0], F_GETFL, 0) | O_NONBLOCK);

		return wake_channel
		{
			.read = unique_fd{fd[0]},
			.write = unique_fd{fd[1]},
		};
	}

	void post (task &t) const noexcept
	{
		const auto p = std::bit_cast<std::uintptr_t>(&t);
		ssize_t r = 0;
		do
		{
			r = ::write(write, &p, sizeof(p));
		} while (r < 0 && errno == EINTR);
	}

	void drain (impl_type &loop) const noexcept;
};

#endif

#if __pal_os_linux
	using poll_array = std::array<::epoll_event, event_loop_config::max_poll_batch>;
	using mmsg_array = std::array<::mmsghdr, event_loop_config::max_drain_batch>;
	using mmsg_buffers_array = std::array<::iovec, event_loop_config::max_drain_batch>;
#elif __pal_os_macos
	using poll_array = std::array<struct ::kevent, event_loop_config::max_poll_batch>;
#elif __pal_os_windows
	using poll_array = std::array<::OVERLAPPED_ENTRY, event_loop_config::max_poll_batch>;
	using unique_port = unique_handle<::HANDLE, ::CloseHandle, nullptr>;
#endif

struct impl_type
{
	clock::time_point now_ = clock::now();
	const std::thread::id loop_thread_ = std::this_thread::get_id();
	task *timer_root_ = nullptr;
	std::atomic<size_t> posts_in_flight_ = 0;
	__task::attorney::task_queue posted_{};
	socket_queue actionable_{};
	__task::attorney::task_queue ready_{};
	event_loop_stats stats_{};
	event_loop_config config_{};

	#if __pal_os_linux
		unique_fd epoll{};
		wake_channel wake{};
		poll_array events{};
		mmsg_array messages{};
		mmsg_buffers_array buffers{};
	#elif __pal_os_macos
		unique_fd kq{};
		wake_channel wake{};
		poll_array events{};
	#elif __pal_os_windows
		unique_port port{};
		poll_array events{};
	#endif

	void poll (clock::duration timeout) noexcept;
	void post (task &t) const noexcept;

	[[nodiscard]] std::error_code register_socket (socket_state &s) const noexcept;
	void drain (socket_state &s, socket_state::direction &d) noexcept;
	void quiesce (socket_state &s) noexcept;
};

result<impl_ptr> make_backend (const event_loop_config &config) noexcept;

#if __pal_os_linux || __pal_os_macos

inline void impl_type::post (task &t) const noexcept
{
	wake.post(t);
}

inline void impl_type::quiesce (socket_state &) noexcept
{
}

inline void wake_channel::drain (impl_type &loop) const noexcept
{
	std::array<std::uintptr_t, 256> tasks;
	size_t total = 0;
	for (;;)
	{
		if (auto r = ::read(read, tasks.data(), sizeof(tasks)); r > 0)
		{
			r /= sizeof(std::uintptr_t);
			for (ssize_t i = 0; i < r; ++i)
			{
				loop.posted_.push(*std::bit_cast<task *>(tasks[i]));
			}
			total += static_cast<size_t>(r);
		}
		else if (r == 0 || errno != EINTR)
		{
			break;
		}
	}
	if (total > 0)
	{
		loop.posts_in_flight_.fetch_sub(total, std::memory_order_relaxed);
	}
}

inline void on_socket_event (socket_state &s, socket_state::direction &d) noexcept
{
	d.ready = true;
	if (!d.pending.empty() && !s.queued)
	{
		s.queued = true;
		s.loop->actionable_.push(s);
	}
}

inline const ::timespec *to_timespec (clock::duration d, ::timespec &storage) noexcept
{
	if (d == clock::duration::max())
	{
		return nullptr;
	}
	const auto wait = (d < clock::duration::zero()) ? clock::duration::zero() : d;
	const auto secs = std::chrono::duration_cast<std::chrono::seconds>(wait);
	const auto nsecs = std::chrono::duration_cast<std::chrono::nanoseconds>(wait - secs);
	storage =
	{
		.tv_sec = static_cast<::time_t>(secs.count()),
		.tv_nsec = static_cast<long>(nsecs.count()),
	};
	return &storage;
}

#endif

} // namespace pal::async::__event_loop
