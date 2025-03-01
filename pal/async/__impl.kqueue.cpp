#include <pal/async/__impl>

#if __pal_async_kqueue
#include <fcntl.h>
#include <sys/event.h>

#if 1
extern "C" {

// Private syscalls:
// https://github.com/apple/darwin-xnu/blob/main/bsd/sys/socket.h#L1371-L1433

// XXX: with quick experimentation, it seems can't use these:
// - any failure in batch fails whole syscall, not single operation
// - does not set MSG_TRUNC in recvmsg_x
//
// Experiment again at some point in future.

struct mmsghdr
{
	struct ::msghdr msg_hdr;
	size_t msg_datalen;
};

ssize_t recvmsg_x (int s, const struct mmsghdr *msgvec, unsigned int vlen, int flags);
ssize_t sendmsg_x (int s, const struct mmsghdr *msgvec, unsigned int vlen, int flags);

} // extern "C"
#endif

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

} // namespace

event_loop::event_loop (std::error_code &error) noexcept
	: handle{::kqueue()}
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

	struct ::kevent ev[2];
	EV_SET(&ev[0], native_socket->handle, EVFILT_WRITE, EV_ADD | EV_CLEAR, 0, 0, async_socket.get());
	EV_SET(&ev[1], native_socket->handle, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, async_socket.get());
	if (::kevent(handle, ev, 2, nullptr, 0, nullptr) == -1)
	{
		return sys_error();
	}

	async_socket->handle = native_socket->handle;
	return async_socket;
}

void event_loop::poll (const std::chrono::milliseconds &duration) noexcept
{
	using namespace std::chrono;

	::timespec timeout{}, *timeout_p = &timeout;
	if (duration == milliseconds::max())
	{
		timeout_p = nullptr;
	}
	else if (duration != milliseconds::zero())
	{
		auto sec = duration_cast<seconds>(duration);
		auto nsec = duration_cast<nanoseconds>(duration - sec);
		timeout.tv_sec = sec.count();
		timeout.tv_nsec = nsec.count();
	}

	struct ::kevent events[max_poll_events];
	auto event_count = ::kevent(handle, nullptr, 0, &events[0], max_poll_events, timeout_p);
	if (event_count < 1)
	{
		event_count = 0;
	}

	for (auto *event = events; event != events + event_count; ++event)
	{
		auto &socket = *static_cast<struct socket *>(event->udata);

		if (event->filter == EVFILT_READ && !socket.receive.corked)
		{
			socket.try_receive_pending();
		}
		else if (event->filter == EVFILT_WRITE && !socket.send.corked)
		{
			socket.try_send_pending();
		}
	}
}

namespace {

template <typename IO>
inline void try_io (socket &socket, task_queue &queue) noexcept
{
	while (auto *task = queue.head())
	{
		auto *io = static_cast<IO *>(task->data());
		if (io->execute(socket))
		{
			queue.pop();
			socket.loop.completed.push(task);
		}
		else
		{
			break;
		}
	}
}

} // namespace

void socket::try_send_pending () noexcept
{
	try_io<struct send>(*this, send.pending);
}

void socket::try_receive_pending () noexcept
{
	try_io<struct receive>(*this, receive.pending);
}

bool receive::execute (socket &socket) noexcept
{
	auto r = ::recvmsg(socket.handle, &message, message.msg_flags | internal_flags);
	if (r > -1)
	{
		message.msg_flags &= ~internal_flags;
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

#endif // __pal_async_kqueue
