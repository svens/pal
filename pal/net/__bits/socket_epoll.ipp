#include <sys/epoll.h>
#include <sys/uio.h>


__pal_begin


namespace net::__bits {


namespace {

constexpr size_t max_mmsg = UIO_MAXIOV;

} // namespace


void socket::impl_type::receive_many (service::notify_fn notify, void *handler) noexcept
{
	::mmsghdr messages[max_mmsg];
	while (!pending_receive.empty())
	{
		auto *it = &messages[0];
		auto *end = make_mmsg(pending_receive.head(), it, &messages[max_mmsg]);
		end = it + ::recvmmsg(handle, it, end - it, internal_flags, nullptr);
		for (/**/;  it < end;  ++it)
		{
			auto *request = pending_receive.pop();
			if (auto *receive_from = std::get_if<async::receive_from>(request))
			{
				receive_from->bytes_transferred = it->msg_len;
				receive_from->flags = it->msg_hdr.msg_flags;
			}
			else if (auto *receive = std::get_if<async::receive>(request))
			{
				receive->bytes_transferred = it->msg_len;
				receive->flags = it->msg_hdr.msg_flags;
			}
			notify(handler, request);
		}

		if (end == it - 1)
		{
			if (is_blocking_error(errno))
			{
				break;
			}

			auto *request = pending_receive.pop();
			request->error.assign(errno, std::generic_category());
			notify(handler, request);
		}
	}
}


void socket::impl_type::send_many (service::notify_fn notify, void *handler) noexcept
{
	// pending_send may contain async_connect request
	//
	// Because we support async_connect() with immediate data send on
	// connection completion, we pass it do sendmmsg().
	// msg_iovlen==0 does not seem to cause any issues

	::mmsghdr messages[max_mmsg];
	while (!pending_send.empty())
	{
		auto *it = &messages[0];
		auto *end = make_mmsg(pending_send.head(), it, &messages[max_mmsg]);
		end = it + ::sendmmsg(handle, it, end - it, internal_flags);
		for (/**/;  it < end;  ++it)
		{
			auto *request = pending_send.pop();
			if (auto *send_to = std::get_if<async::send_to>(request))
			{
				send_to->bytes_transferred = it->msg_len;
			}
			else if (auto *send = std::get_if<async::send>(request))
			{
				send->bytes_transferred = it->msg_len;
			}
			else if (auto *connect = std::get_if<async::connect>(request))
			{
				connect->bytes_transferred = it->msg_len;
			}
			notify(handler, request);
		}

		if (end == it - 1)
		{
			if (is_blocking_error(errno))
			{
				break;
			}
			else if (is_connection_error(errno))
			{
				errno = ENOTCONN;
			}

			auto *request = pending_send.pop();
			request->error.assign(errno, std::generic_category());
			notify(handler, request);
		}
	}
}


service::service (std::error_code &error) noexcept
	: impl{new(std::nothrow) impl_type}
{
	if (impl)
	{
		impl->handle = ::epoll_create1(0);
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

	::epoll_event ev;
	ev.data.ptr = socket.impl.get();
	ev.events = EPOLLET | EPOLLIN | EPOLLOUT;
	if (::epoll_ctl(impl->handle, EPOLL_CTL_ADD, socket.impl->handle, &ev) == -1)
	{
		return sys_error();
	}

	return {};
}


void service::poll_for (
	const std::chrono::milliseconds &duration,
	notify_fn notify,
	void *handler) noexcept
{
	int timeout = duration.count();
	if (!impl->completed.empty())
	{
		timeout = 0;
	}

	// completions since last poll
	impl->notify(notify, handler);

	::epoll_event events[max_poll_events];
	auto event_count = ::epoll_wait(impl->handle, &events[0], max_poll_events, timeout);
	if (event_count < 1)
	{
		event_count = 0;
	}

	for (auto *event = events;  event != events + event_count;  ++event)
	{
		auto &socket = *static_cast<socket::impl_type *>(event->data.ptr);
		if (event->events & EPOLLERR)
		{
			socket.cancel(notify, handler, socket.pending_error());
		}
		else
		{
			if (event->events & EPOLLOUT)
			{
				socket.send_many(notify, handler);
			}
			if (event->events & EPOLLIN)
			{
				if (socket.is_acceptor)
				{
					socket.accept_many(notify, handler);
				}
				else
				{
					socket.receive_many(notify, handler);
				}
			}
			if (event->events & (EPOLLRDHUP | EPOLLHUP))
			{
				socket.cancel(notify, handler, 0);
			}
		}
	}

	// completions since this poll
	impl->notify(notify, handler);
}


} // namespace net::__bits


__pal_end
