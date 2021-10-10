#include <sys/epoll.h>


__pal_begin


namespace net::__bits {


void socket::impl_type::receive_one (async::request *request,
	size_t &bytes_transferred,
	message_flags &flags) noexcept
{
	auto r = ::recvmsg(handle, &request->impl_.message, request->impl_.message.msg_flags);
	if (r > -1)
	{
		bytes_transferred = r;
		flags = request->impl_.message.msg_flags;
	}
	else if (is_blocking_error(errno))
	{
		pending_receive.push(request);
		return;
	}
	else
	{
		request->error.assign(errno, std::generic_category());
	}
	service->completed.push(request);
}


void socket::impl_type::send_one (async::request *request, size_t &bytes_transferred) noexcept
{
	auto r = ::sendmsg(handle, &request->impl_.message, request->impl_.message.msg_flags);
	if (r > -1)
	{
		bytes_transferred = r;
	}
	else if (is_blocking_error(errno))
	{
		pending_send.push(request);
		return;
	}
	else if (errno == EDESTADDRREQ || errno == EPIPE)
	{
		request->error.assign(ENOTCONN, std::generic_category());
	}
	else
	{
		request->error.assign(errno, std::generic_category());
	}
	service->completed.push(request);
}


void socket::impl_type::receive_many (service::completion_fn process, void *listener) noexcept
{
	::mmsghdr messages[max_mmsg];
	while (!pending_receive.empty())
	{
		auto *first = &messages[0];
		auto *last = make_mmsg(pending_receive.head(), first, &messages[max_mmsg]);
		last = first + ::recvmmsg(handle, first, last - first, internal_flags, nullptr);
		for (/**/;  first < last;  ++first)
		{
			auto *request = pending_receive.pop();
			if (auto *receive_from = std::get_if<async::receive_from>(request))
			{
				receive_from->bytes_transferred = first->msg_len;
				receive_from->flags = first->msg_hdr.msg_flags;
			}
			else if (auto *receive = std::get_if<async::receive>(request))
			{
				receive->bytes_transferred = first->msg_len;
				receive->flags = first->msg_hdr.msg_flags;
			}
			process(listener, request);
		}

		if (first != last)
		{
			break;
		}
	}
}


void socket::impl_type::send_many (service::completion_fn process, void *listener) noexcept
{
	// pending_send may contain async_connect request
	//
	// Because we support async_connect() with immediate data send on
	// connection completion, we pass it do sendmmsg().
	// msg_iovlen==0 does not seem to cause any issues

	::mmsghdr messages[max_mmsg];
	while (!pending_send.empty())
	{
		auto *first = &messages[0];
		auto *last = make_mmsg(pending_send.head(), first, &messages[max_mmsg]);
		last = first + ::sendmmsg(handle, first, last - first, internal_flags);
		for (/**/;  first < last;  ++first)
		{
			auto *request = pending_send.pop();
			if (auto *send_to = std::get_if<async::send_to>(request))
			{
				send_to->bytes_transferred = first->msg_len;
			}
			else if (auto *send = std::get_if<async::send>(request))
			{
				send->bytes_transferred = first->msg_len;
			}
			else if (auto *connect = std::get_if<async::connect>(request))
			{
				connect->bytes_transferred = first->msg_len;
			}
			process(listener, request);
		}

		if (first != last)
		{
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
	const std::chrono::milliseconds &poll_duration,
	completion_fn process,
	void *listener) noexcept
{
	int timeout = -1;
	if (!impl->completed.empty() || poll_duration == poll_duration.zero())
	{
		timeout = 0;
	}
	else if (poll_duration != poll_duration.max())
	{
		timeout = poll_duration.count();
	}

	// completions since last poll
	impl->notify(process, listener);

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
			socket.cancel(process, listener, socket.pending_error());
		}
		else
		{
			if (event->events & EPOLLOUT)
			{
				socket.send_many(process, listener);
			}
			if (event->events & EPOLLIN)
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
			if (event->events & (EPOLLRDHUP | EPOLLHUP))
			{
				socket.cancel(process, listener, 0);
			}
		}
	}

	// completions since this poll
	impl->notify(process, listener);
}


} // namespace net::__bits


__pal_end
