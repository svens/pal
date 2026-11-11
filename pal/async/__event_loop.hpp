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
	#include <climits>
	#include <fcntl.h>
	#include <sys/socket.h>
	#include <sys/uio.h>
	#include <time.h>
	#include <unistd.h>
#endif

#if __pal_os_linux
	#include <sys/epoll.h>
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
	const int family;
	direction receive{}, send{};
	bool queued = false;
	socket_kind kind;
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

#if __pal_os_linux || __pal_os_macos
	static_assert(event_loop_config::max_drain_batch <= IOV_MAX);
	using io_vector_array = std::array<::iovec, event_loop_config::max_drain_batch>;
#endif

#if __pal_os_linux
	using poll_array = std::array<::epoll_event, event_loop_config::max_poll_batch>;
	using mmsg_array = std::array<::mmsghdr, event_loop_config::max_drain_batch>;
#elif __pal_os_macos
	using poll_array = std::array<struct ::kevent, event_loop_config::max_poll_batch>;
#elif __pal_os_windows
	using poll_array = std::array<::OVERLAPPED_ENTRY, event_loop_config::max_poll_batch>;
	using unique_port = unique_handle<::HANDLE, ::CloseHandle, nullptr>;
#endif

struct impl_type
{
	clock::time_point now_{};
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
		io_vector_array buffers{};
	#elif __pal_os_macos
		unique_fd kq{};
		wake_channel wake{};
		poll_array events{};
		io_vector_array buffers{};
	#elif __pal_os_windows
		unique_port port{};
		poll_array events{};
	#endif

	std::error_code poll (clock::duration timeout) noexcept;
	void post (task &t) const noexcept;

	[[nodiscard]] std::error_code register_socket (socket_state &s) const noexcept;
	void connect (socket_state &s, task &t) noexcept;
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

inline void impl_type::connect (socket_state &s, task &t) noexcept
{
	auto &op = io(t);
	const auto *endpoint = reinterpret_cast<const ::sockaddr *>(op.endpoint.data());
	if (::connect(static_cast<int>(s.handle), endpoint, op.endpoint_size) == 0)
	{
		ready_.push(t);
	}
	else if (errno == EINPROGRESS)
	{
		s.kind = socket_kind::connecting;
		s.send.ready = false;
		s.send.pending.push(t);
	}
	else
	{
		op.native_error = errno;
		ready_.push(t);
	}
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

inline void drain_stream_send (impl_type &loop, socket_state &s, socket_state::direction &d) noexcept
{
	size_t count = 0;
	for (auto &t: d.pending)
	{
		if (count == loop.config_.drain_batch)
		{
			break;
		}
		const auto window = t.span().subspan(io(t).bytes);
		loop.buffers[count++] = {.iov_base = window.data(), .iov_len = window.size()};
	}

	::msghdr message{};
	message.msg_iov = loop.buffers.data();
	message.msg_iovlen = static_cast<decltype(message.msg_iovlen)>(count);

	ssize_t r = 0;
	do
	{
		r = ::sendmsg(static_cast<int>(s.handle), &message, MSG_NOSIGNAL);
	} while (r < 0 && errno == EINTR);

	if (r < 0)
	{
		static_assert(EAGAIN == EWOULDBLOCK);
		if (errno == EAGAIN)
		{
			d.ready = false;
		}
		else
		{
			task *t = d.pending.pop();
			io(*t).native_error = errno;
			loop.ready_.push(*t);
		}
		return;
	}

	auto sent = static_cast<size_t>(r);
	for (size_t i = 0; i < count; ++i)
	{
		auto &t = *d.pending.head();
		auto &op = io(t);
		const auto left = t.span().size() - op.bytes;
		if (sent < left)
		{
			op.bytes += sent;
			return;
		}
		sent -= left;
		loop.ready_.push(*d.pending.pop());
	}
}

// TODO(svens): one recvmsg across all pending receives, like drain_stream_send, if apps start keeping several
// receives armed; not done because the typical app validates each chunk before re-arming its single receive
inline void drain_stream_receive (impl_type &loop, socket_state &s, socket_state::direction &d) noexcept
{
	const auto fd = static_cast<int>(s.handle);
	for (size_t i = 0; i < loop.config_.drain_batch && d.actionable(); ++i)
	{
		auto &t = *d.pending.head();
		auto &op = io(t);
		const auto window = t.span();

		ssize_t r = 0;
		do
		{
			r = ::recv(fd, window.data(), window.size(), 0);
		} while (r < 0 && errno == EINTR);

		if (r < 0)
		{
			static_assert(EAGAIN == EWOULDBLOCK);
			if (errno == EAGAIN)
			{
				d.ready = false;
				return;
			}
			op.native_error = errno;
		}
		else
		{
			op.bytes = static_cast<size_t>(r);
		}
		loop.ready_.push(*d.pending.pop());
	}
}

inline void drain_stream (impl_type &loop, socket_state &s, socket_state::direction &d) noexcept
{
	if (&d == &s.send)
	{
		drain_stream_send(loop, s, d);
	}
	else
	{
		drain_stream_receive(loop, s, d);
	}
}

inline void drain_connecting (impl_type &loop, socket_state &s, socket_state::direction &d) noexcept
{
	const auto fd = static_cast<int>(s.handle);
	auto &op = io(*d.pending.head());

	int error = 0;
	::socklen_t error_size = sizeof(error);
	std::ignore = ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &error_size);
	if (error == 0)
	{
		::sockaddr_storage peer{};
		::socklen_t peer_size = sizeof(peer);
		if (::getpeername(fd, reinterpret_cast<::sockaddr *>(&peer), &peer_size) == -1)
		{
			if (errno == ENOTCONN)
			{
				d.ready = false;
				return;
			}
			error = errno;
		}
	}

	op.native_error = error;
	s.kind = socket_kind::stream;
	loop.ready_.push(*d.pending.pop());
}

inline void drain_accept (impl_type &loop, socket_state &s, socket_state::direction &d) noexcept
{
	const auto fd = static_cast<int>(s.handle);
	for (size_t i = 0; i < loop.config_.drain_batch && d.actionable(); ++i)
	{
		auto &t = *d.pending.head();
		auto &op = io(t);

		::socklen_t size = io_state::endpoint_capacity;
		int r = 0;
		do
		{
			r = ::accept(fd, reinterpret_cast<::sockaddr *>(op.endpoint.data()), &size);
		} while (r < 0 && errno == EINTR);

		if (r < 0)
		{
			static_assert(EAGAIN == EWOULDBLOCK);
			if (errno == EAGAIN)
			{
				d.ready = false;
				return;
			}
			op.native_error = errno;
		}
		else if (os == os_type::macos && size == 0)
		{
			::close(r);
			op.native_error = ECONNABORTED;
		}
		else
		{
			op.accepted = r;
		}
		loop.ready_.push(*d.pending.pop());
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
