#include <pal/async/__impl>

#if __pal_async_epoll
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/uio.h>

namespace pal::async::__impl {

using namespace net::__socket;

namespace {

constexpr auto internal_flags = MSG_DONTWAIT | MSG_NOSIGNAL;

constexpr bool is_blocking_error (int v) noexcept
{
	if constexpr (EAGAIN != EWOULDBLOCK)
	{
		return v == EAGAIN || v == EWOULDBLOCK;
	}
	else
	{
		return v == EWOULDBLOCK;
	}
}

constexpr bool is_connection_error (int v) noexcept
{
	return v == EDESTADDRREQ || v == EPIPE;
}

auto pending_error (net::native_socket_handle::value_type handle) noexcept
{
	int error = 0;
	socklen_t error_size = sizeof(error);
	if (::getsockopt(handle, SOL_SOCKET, SO_ERROR, &error, &error_size) == -1)
	{
		// getsockopt error, not pending socket error, but nothing we can do here
		error = errno;
	}
	return sys_error(error);
}

} // namespace

event_loop::event_loop (std::error_code &error) noexcept
	: handle{::epoll_create1(0)}
{
	if (handle == -1)
	{
		error.assign(errno, std::generic_category());
		return;
	}

	error.clear();
}

event_loop::~event_loop () noexcept
{
	if (handle != -1)
	{
		while (true)
		{
			if (::close(handle) == 0 || errno != EINTR)
			{
				return;
			}
		}
	}
}

result<socket_ptr> event_loop::add_socket (const net::native_socket &native_socket) noexcept
{
	auto flags = ::fcntl(native_socket->handle, F_GETFL, 0);
	if (flags == -1 || ::fcntl(native_socket->handle, F_SETFL, flags | O_NONBLOCK) == -1)
	{
		return sys_error();
	}

	auto async_socket = socket_ptr{new(std::nothrow) __impl::socket{.loop = *this}};
	if (!async_socket)
	{
		return make_unexpected(std::errc::not_enough_memory);
	}

	::epoll_event ev
	{
		.events = EPOLLET | EPOLLIN | EPOLLOUT,
		.data = { .ptr = async_socket.get(), },
	};
	if (::epoll_ctl(handle, EPOLL_CTL_ADD, native_socket->handle, &ev) == -1)
	{
		return sys_error();
	}

	async_socket->handle = native_socket->handle;
	return async_socket;
}

void event_loop::poll (const std::chrono::milliseconds &duration) noexcept
{
	::epoll_event events[max_poll_events];
	auto event_count = ::epoll_wait(handle, &events[0], max_poll_events, duration.count());
	if (event_count < 1)
	{
		event_count = 0;
	}

	for (auto *event = events; event != events + event_count; ++event)
	{
		auto &socket = *static_cast<struct socket *>(event->data.ptr);

		if (event->events & EPOLLERR)
		{
			auto result = pending_error(socket.handle);
			socket.cancel(socket.receive.pending, result);
			socket.cancel(socket.send.pending, result);
			continue;
		}

		if (event->events & EPOLLIN && !socket.receive.corked)
		{
			socket.try_receive_pending();
		}

		if (event->events & EPOLLOUT && !socket.send.corked)
		{
			socket.try_send_pending();
		}

		if (events->events & (EPOLLRDHUP | EPOLLHUP))
		{
			auto result = make_unexpected(std::errc::connection_aborted);
			socket.cancel(socket.receive.pending, result);
			socket.cancel(socket.send.pending, result);
		}
	}
}

namespace {

constexpr size_t max_mmsg = 64;

::mmsghdr *make_mmsg (task *task, ::mmsghdr *it, const ::mmsghdr *end) noexcept
{
	for (/**/; task && it != end; ++it, task = task->next)
	{
		it->msg_hdr = static_cast<io_task *>(task->data())->message;
	}
	return it;
}

} // namespace

void socket::try_send_pending () noexcept
{
	auto &pending = send.pending;
	auto &completed = loop.completed;

	while (!pending.empty())
	{
		::mmsghdr messages[max_mmsg];
		auto *it = &messages[0];
		auto *end = make_mmsg(pending.head(), it, &messages[max_mmsg]);
		auto *last = it + ::sendmmsg(handle, it, end - it, internal_flags);

		for (/**/; it < last; ++it)
		{
			auto *task = pending.pop();
			static_cast<io_task *>(task->data())->result = it->msg_len;
			completed.push(task);
		}

		if (last == it - 1)
		{
			if (is_blocking_error(errno))
			{
				break;
			}
			else if (is_connection_error(errno))
			{
				errno = ENOTCONN;
			}

			auto *task = pending.pop();
			static_cast<io_task *>(task->data())->result = sys_error();
			completed.push(task);
		}
	}
}

void socket::try_receive_pending () noexcept
{
	auto &pending = receive.pending;
	auto &completed = loop.completed;

	while (!pending.empty())
	{
		::mmsghdr messages[max_mmsg];
		auto *it = &messages[0];
		auto *end = make_mmsg(pending.head(), it, &messages[max_mmsg]);
		auto *last = it + ::recvmmsg(handle, it, end - it, internal_flags, nullptr);

		for (/**/; it < last; ++it)
		{
			auto *task = pending.pop();
			auto &io = *static_cast<io_task *>(task->data());
			io.result = it->msg_len;
			io.message.msg_flags = it->msg_hdr.msg_flags;
			completed.push(task);
		}

		if (last == it - 1)
		{
			if (is_blocking_error(errno))
			{
				break;
			}

			auto *task = pending.pop();
			static_cast<io_task *>(task->data())->result = sys_error();
			completed.push(task);
		}
	}
}

bool receive::execute (socket &socket) noexcept
{
	auto r = ::recvmsg(socket.handle, &message, message.msg_flags | internal_flags);
	if (r > -1)
	{
		result = r;
		return true;
	}
	else if (is_blocking_error(errno))
	{
		return false;
	}
	result = sys_error();
	return true;
}

bool send::execute (socket &socket) noexcept
{
	auto r = ::sendmsg(socket.handle, &message, message.msg_flags | internal_flags);
	if (r > -1)
	{
		result = r;
		return true;
	}
	else if (is_blocking_error(errno))
	{
		return false;
	}
	else if (is_connection_error(errno))
	{
		errno = ENOTCONN;
	}
	result = sys_error();
	return true;
}

} // namespace pal::async::__impl

#endif // __pal_async_epoll
