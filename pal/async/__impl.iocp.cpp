#include <pal/async/__impl>

#if __pal_async_iocp

namespace pal::async::__impl {

using namespace net::__socket;

event_loop::event_loop (std::error_code &error) noexcept
	: handle{::CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 1)}
{
	if (handle == INVALID_HANDLE_VALUE)
	{
		error.assign(::GetLastError(), std::system_category());
		return;
	}

	error.clear();
}

event_loop::~event_loop () noexcept
{
	if (handle != INVALID_HANDLE_VALUE)
	{
		(void)::CloseHandle(handle);
	}
}

result<socket_ptr> event_loop::add_socket (const net::native_socket &native_socket) noexcept
{
	auto async_socket = socket_ptr{new(std::nothrow) __impl::socket{.loop = *this}};
	if (!async_socket)
	{
		return make_unexpected(std::errc::not_enough_memory);
	}

	auto r = ::CreateIoCompletionPort(
		reinterpret_cast<HANDLE>(native_socket->handle),
		handle,
		reinterpret_cast<ULONG_PTR>(async_socket.get()),
		1
	);
	if (r)
	{
		async_socket->handle = native_socket->handle;
		return async_socket;
	}

	return sys_error();
}

void event_loop::poll (const std::chrono::milliseconds &duration) noexcept
{
	::OVERLAPPED_ENTRY events[max_poll_events];
	ULONG event_count = 0;

	auto rv = ::GetQueuedCompletionStatusEx(
		handle,
		events,
		max_poll_events,
		&event_count,
		static_cast<DWORD>(duration.count()),
		false
	);

	if (!rv)
	{
		event_count = 0;
	}

	for (auto *event = events; event != events + event_count; ++event)
	{
		auto *socket = reinterpret_cast<struct socket *>(event->lpCompletionKey);

		DWORD bytes_transferred = 0, flags = 0;
		rv = ::WSAGetOverlappedResult(
			socket->handle,
			event->lpOverlapped,
			&bytes_transferred,
			false,
			&flags
		);

		if (rv == FALSE)
		{
			rv = ::WSAGetLastError();
			if (rv == WSAEMSGSIZE)
			{
				flags |= MSG_TRUNC;
				rv = 0;
			}
			else if (rv == WSAENOTSOCK || rv == WSA_INVALID_HANDLE)
			{
				rv = WSAEBADF;
			}
		}
		else
		{
			rv = 0;
		}

		auto *data = reinterpret_cast<task_base *>(event->lpOverlapped);
		if (data->kind == io_task::kind_value)
		{
			auto *io = static_cast<io_task *>(data);
			if (rv == 0)
			{
				io->result = bytes_transferred;
				io->message.msg_flags = flags;
			}
			else
			{
				io->result = sys_error(rv);
			}
		}

		completed.push(data->owner);
	}
}

void socket::try_send_pending () noexcept
{
	while (auto *task = send.pending.try_pop())
	{
		auto *data = static_cast<task_base *>(task->data());
		if (data->kind == send::kind_value)
		{
			auto *io = static_cast<struct send *>(data);
			if (io->execute(*this))
			{
				loop.completed.push(task);
			}
		}
	}
}

void socket::try_receive_pending () noexcept
{
	while (auto *task = receive.pending.try_pop())
	{
		auto *data = static_cast<task_base *>(task->data());
		if (data->kind == receive::kind_value)
		{
			auto *io = static_cast<struct receive *>(data);
			if (io->execute(*this))
			{
				loop.completed.push(task);
			}
		}
	}
}

namespace {

bool continue_task (int rv, DWORD bytes_transferred, auto &request) noexcept
{
	if (rv == 0)
	{
		request.result = bytes_transferred;
		return true;
	}

	auto e = ::WSAGetLastError();
	if (e != WSA_IO_PENDING)
	{
		if (e == WSAENOTSOCK || e == WSA_INVALID_HANDLE)
		{
			// unify with Posix
			e = WSAEBADF;
		}
		request.result = sys_error(e);
		return true;
	}

	return false;
}

} // namespace

bool receive::execute (socket &socket) noexcept
{
	int rv;
	DWORD bytes_transferred = 0;
	if (message.msg_name)
	{
		rv = ::WSARecvFrom(
			socket.handle,
			message.msg_iov.data(),
			message.msg_iovlen,
			&bytes_transferred,
			&message.msg_flags,
			message.msg_name,
			&message.msg_namelen,
			&overlapped,
			nullptr
		);
	}
	else
	{
		rv = ::WSARecv(
			socket.handle,
			message.msg_iov.data(),
			message.msg_iovlen,
			&bytes_transferred,
			&message.msg_flags,
			&overlapped,
			nullptr
		);
	}

	return continue_task(rv, bytes_transferred, *this);
}

bool send::execute (socket &socket) noexcept
{
	int rv;
	DWORD bytes_transferred = 0;
	if (message.msg_name)
	{
		rv = ::WSASendTo(
			socket.handle,
			message.msg_iov.data(),
			message.msg_iovlen,
			&bytes_transferred,
			message.msg_flags,
			message.msg_name,
			message.msg_namelen,
			&overlapped,
			nullptr
		);
	}
	else
	{
		rv = ::WSASend(
			socket.handle,
			message.msg_iov.data(),
			message.msg_iovlen,
			&bytes_transferred,
			message.msg_flags,
			&overlapped,
			nullptr
		);
	}

	return continue_task(rv, bytes_transferred, *this);
}

} // namespace pal::async::__impl

#endif // __pal_async_iocp
