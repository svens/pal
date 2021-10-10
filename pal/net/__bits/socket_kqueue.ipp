#include <sys/event.h>


__pal_begin


namespace net::__bits {


namespace {

inline bool await (int queue, uintptr_t handle, int16_t filter, uint16_t flags, void *udata) noexcept
{
	constexpr uint32_t fflags = 0;
	intptr_t data = 0;
	struct ::kevent change;
	EV_SET(&change, handle, filter, flags, fflags, data, udata);
	return ::kevent(queue, &change, 1, nullptr, 0, nullptr) > -1;
}

} // namespace


bool socket::impl_type::await_read () noexcept
{
	return await(service->handle, handle, EVFILT_READ, EV_ADD | EV_CLEAR, this);
}


bool socket::impl_type::await_write () noexcept
{
	return await(service->handle, handle, EVFILT_WRITE, EV_ADD | EV_CLEAR, this);
}


void socket::impl_type::receive_many (service::completion_fn process, void *listener) noexcept
{
	// TODO: recvmsg_x
	while (auto *it = pending_receive.head())
	{
		auto r = ::recvmsg(handle, &it->impl_.message, it->impl_.message.msg_flags);
		if (r > -1)
		{
			it->impl_.message.msg_flags &= ~internal_flags;
			if (auto *receive_from = std::get_if<async::receive_from>(it))
			{
				receive_from->bytes_transferred = r;
				receive_from->flags = it->impl_.message.msg_flags;
			}
			else if (auto *receive = std::get_if<async::receive>(it))
			{
				receive->bytes_transferred = r;
				receive->flags = it->impl_.message.msg_flags;
			}
			process(listener, pending_receive.pop());
		}
		else
		{
			if (!is_blocking_error(errno) || !await_read())
			{
				it->error.assign(errno, std::generic_category());
				process(listener, pending_receive.pop());
			}
			break;
		}
	}
}


void socket::impl_type::send_many (service::completion_fn process, void *listener) noexcept
{
	// pending_send may contain async_connect request
	//
	// Because we support async_connect() with immediate data send on
	// connection completion, we pass it do sendmsg(). If msg_iovlen is 0,
	// we ignore errors

	// TODO: sendmsg_x
	while (auto *it = pending_send.head())
	{
		auto r = ::sendmsg(handle, &it->impl_.message, it->impl_.message.msg_flags);
		if (r > -1)
		{
			if (auto *send_to = std::get_if<async::send_to>(it))
			{
				send_to->bytes_transferred = r;
			}
			else if (auto *send = std::get_if<async::send>(it))
			{
				send->bytes_transferred = r;
			}
			else if (auto *connect = std::get_if<async::connect>(it))
			{
				connect->bytes_transferred = r;
			}
			process(listener, pending_send.pop());
		}
		else if (errno == EMSGSIZE && it->impl_.message.msg_iovlen == 0)
		{
			if (auto *connect = std::get_if<async::connect>(it))
			{
				connect->bytes_transferred = 0;
			}
			process(listener, pending_send.pop());
		}
		else
		{
			if (!is_blocking_error(errno) || !await_write())
			{
				it->error.assign(errno, std::generic_category());
				process(listener, pending_send.pop());
			}
			break;
		}
	}
}


void socket::impl_type::accept_many (service::completion_fn process, void *listener) noexcept
{
	(void)process;
	(void)listener;
}


service::service (std::error_code &error) noexcept
	: impl{new(std::nothrow) impl_type}
{
	if (impl)
	{
		impl->handle = ::kqueue();
		if (impl->handle == -1)
		{
			error.assign(errno, std::generic_category());
		}
	}
	else
	{
		error = std::make_error_code(std::errc::not_enough_memory);
	}
}


result<void> service::add (__bits::socket &socket) noexcept
{
	socket.impl->service = impl.get();
	auto flags = ::fcntl(socket.impl->handle, F_GETFL, 0);
	if (flags == -1 || ::fcntl(socket.impl->handle, F_SETFL, flags | O_NONBLOCK) == -1)
	{
		return sys_error();
	}
	return {};
}


void service::poll_for (
	const std::chrono::milliseconds &poll_duration,
	completion_fn process,
	void *listener) noexcept
{
	::timespec timeout, *timeout_p = nullptr;
	if (!impl->completed.empty() || poll_duration == poll_duration.zero())
	{
		timeout.tv_sec = timeout.tv_nsec = 0;
		timeout_p = &timeout;
	}
	else if (poll_duration != poll_duration.max())
	{
		auto sec = std::chrono::duration_cast<std::chrono::seconds>(poll_duration);
		auto nsec = std::chrono::duration_cast<std::chrono::nanoseconds>(poll_duration - sec);
		timeout.tv_sec = sec.count();
		timeout.tv_nsec = nsec.count();
		timeout_p = &timeout;
	}

	// completions since last poll
	impl->notify(process, listener);

	struct ::kevent events[max_poll_events];
	auto event_count = ::kevent(impl->handle,
		nullptr, 0,
		&events[0], max_poll_events,
		timeout_p
	);

	if (event_count < 1)
	{
		event_count = 0;
	}

	for (auto *event = events;  event != events + event_count;  ++event)
	{
		auto &socket = *static_cast<socket::impl_type *>(event->udata);
		if (event->filter == EVFILT_READ)
		{
			if (socket.is_acceptor)
			{
				socket.accept_many(process, listener);
			}
			else
			{
				socket.receive_many(process, listener);
			}
		}
		else if (event->filter == EVFILT_WRITE)
		{
			socket.send_many(process, listener);
		}
	}

	// completions since this poll
	impl->notify(process, listener);
}


} // namespace net::__bits


__pal_end
