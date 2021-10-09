__pal_begin


namespace net::__bits {


namespace {

async::request *owner_of (::OVERLAPPED_ENTRY &event) noexcept
{
	// During asynchronous request start, library passes
	// async::request::overlapped_ pointer to OS. On completion, OS returns it in
	// event->lpOverlapped. We can get original request address from it using this
	// offset
	static const size_t overlapped_offset =
		reinterpret_cast<char *>(&static_cast<async::request *>(nullptr)->overlapped_)
		-
		static_cast<char *>(nullptr)
	;
	return reinterpret_cast<async::request *>(
		reinterpret_cast<char *>(event.lpOverlapped) - overlapped_offset
	);
}

bool pre_check (async::request *request, service::impl_type *service) noexcept
{
	if (!request->error)
	{
		request->overlapped_ = {};
		return true;
	}
	else
	{
		service->completed.push(request);
		return false;
	}
}

bool post_check (async::request *request, service::impl_type *service, int result) noexcept
{
	if (result == 0)
	{
		// note: caller should fill in values within request
		service->completed.push(request);
		return true;
	}
	else if (int error = ::WSAGetLastError();  error != WSA_IO_PENDING)
	{
		if (error == WSAENOTSOCK || error == WSA_INVALID_HANDLE)
		{
			// unify with Posix
			error = WSAEBADF;
		}
		request->error.assign(error, std::system_category());
		service->completed.push(request);
	}
	return false;
}

bool is_loopback_v4 (const ::sockaddr *name) noexcept
{
	auto *bytes = reinterpret_cast<const uint8_t *>(
		&reinterpret_cast<const ::sockaddr_in *>(name)->sin_addr.s_addr
	);
	return (bytes[0] & 0xff) == 0x7f;
}

bool is_loopback_v6 (const ::sockaddr *name) noexcept
{
	static constexpr uint8_t loopback_bytes[] = IN6ADDR_LOOPBACK_INIT;
	auto *bytes = reinterpret_cast<const uint8_t *>(
		&reinterpret_cast<const ::sockaddr_in6 *>(name)->sin6_addr
	);
	return bytes == loopback_bytes;
}

bool try_bind (socket::impl_type &socket, const message &message) noexcept
{
	::sockaddr_storage bind_address{};

	switch (bind_address.ss_family = message.msg_name->sa_family)
	{
		case AF_INET:
		{
			auto &local = *reinterpret_cast<::sockaddr_in *>(&bind_address);
			if (is_loopback_v4(message.msg_name))
			{
				local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
			}
			else
			{
				local.sin_addr.s_addr = htonl(INADDR_ANY);
			}
		}
		break;

		case AF_INET6:
		{
			auto &local = *reinterpret_cast<::sockaddr_in6 *>(&bind_address);
			if (is_loopback_v6(message.msg_name))
			{
				local.sin6_addr = IN6ADDR_LOOPBACK_INIT;
			}
			else
			{
				local.sin6_addr = IN6ADDR_ANY_INIT;
			}
		}
		break;
	}

	auto r = ::bind(socket.handle,
		reinterpret_cast<const ::sockaddr *>(&bind_address),
		message.msg_namelen
	);

	return r != -1;
}

} // namespace


bool socket::has_async () const noexcept
{
	return impl->service != nullptr;
}


void socket::start (async::receive_from &receive_from) noexcept
{
	auto *request = owner_of(receive_from);
	if (!pre_check(request, impl->service))
	{
		return;
	}

	DWORD bytes_transferred{};
	auto result = ::WSARecvFrom(
		impl->handle,
		request->impl_.message.msg_iov.data(),
		request->impl_.message.msg_iovlen,
		&bytes_transferred,
		&request->impl_.message.msg_flags,
		request->impl_.message.msg_name,
		&request->impl_.message.msg_namelen,
		&request->overlapped_,
		nullptr
	);

	if (post_check(request, impl->service, result))
	{
		receive_from.bytes_transferred = bytes_transferred;
		receive_from.flags = request->impl_.message.msg_flags;
	}
}


void socket::start (async::receive &receive) noexcept
{
	auto *request = owner_of(receive);
	if (!pre_check(request, impl->service))
	{
		return;
	}

	DWORD bytes_transferred{};
	auto result = ::WSARecv(
		impl->handle,
		request->impl_.message.msg_iov.data(),
		request->impl_.message.msg_iovlen,
		&bytes_transferred,
		&request->impl_.message.msg_flags,
		&request->overlapped_,
		nullptr
	);

	if (post_check(request, impl->service, result))
	{
		receive.bytes_transferred = bytes_transferred;
		receive.flags = request->impl_.message.msg_flags;
	}
}


void socket::start (async::send_to &send_to) noexcept
{
	auto *request = owner_of(send_to);
	if (!pre_check(request, impl->service))
	{
		return;
	}

	DWORD bytes_transferred{};
	auto result = ::WSASendTo(
		impl->handle,
		request->impl_.message.msg_iov.data(),
		request->impl_.message.msg_iovlen,
		&bytes_transferred,
		request->impl_.message.msg_flags,
		request->impl_.message.msg_name,
		request->impl_.message.msg_namelen,
		&request->overlapped_,
		nullptr
	);

	if (post_check(request, impl->service, result))
	{
		send_to.bytes_transferred = bytes_transferred;
	}
}


void socket::start (async::send &send) noexcept
{
	auto *request = owner_of(send);
	if (!pre_check(request, impl->service))
	{
		return;
	}

	DWORD bytes_transferred{};
	auto result = ::WSASend(
		impl->handle,
		request->impl_.message.msg_iov.data(),
		request->impl_.message.msg_iovlen,
		&bytes_transferred,
		request->impl_.message.msg_flags,
		&request->overlapped_,
		nullptr
	);

	if (post_check(request, impl->service, result))
	{
		send.bytes_transferred = bytes_transferred;
	}
}


void socket::start (async::connect &connect) noexcept
{
	auto *request = owner_of(connect);
	request->overlapped_ = {};

	// ConnectEx() requires socket to be bound, Posix connect() does not
	//
	// Align behaviour with Posix: if 1st ConnectEx() fails with
	// WSAEINVAL, bind and retry

retry:
	DWORD bytes_transferred{};
	int result = (*ConnectEx)(
		impl->handle,
		request->impl_.message.msg_name,
		request->impl_.message.msg_namelen,
		nullptr,
		0,
		&bytes_transferred,
		&request->overlapped_
	);

	if (result == FALSE)
	{
		result = ::WSAGetLastError();
		if (result == ERROR_IO_PENDING)
		{
			return;
		}
		else if (result == WSAEINVAL && try_bind(*impl, request->impl_.message))
		{
			goto retry;
		}
		else if (result == WSAENOTSOCK)
		{
			result = WSAEBADF;
		}
	}
	else
	{
		result = ::setsockopt(
			impl->handle,
			SOL_SOCKET,
			SO_UPDATE_CONNECT_CONTEXT,
			nullptr,
			0
		);
		if (result == 0)
		{
			connect.bytes_transferred = bytes_transferred;
		}
		else
		{
			result = ::WSAGetLastError();
		}
	}

	if (result == 0)
	{
		request->error.clear();
	}
	else
	{
		request->error.assign(result, std::system_category());
	}
	impl->service->completed.push(request);
}


service::service (std::error_code &error) noexcept
	: impl{new(std::nothrow) impl_type}
{
	if (impl)
	{
		impl->handle = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 1);
		if (impl->handle == INVALID_HANDLE_VALUE)
		{
			error.assign(::GetLastError(), std::system_category());
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

	auto result = ::CreateIoCompletionPort(
		reinterpret_cast<HANDLE>(socket.impl->handle),
		impl->handle,
		reinterpret_cast<ULONG_PTR>(socket.impl.get()),
		1
	);
	if (result)
	{
		return {};
	}
	return sys_error();
}


void service::poll_for (
	const std::chrono::milliseconds &poll_duration,
	completion_fn process,
	void *listener) noexcept
{
	DWORD timeout = INFINITE, bytes_transferred, flags;
	if (!impl->completed.empty() || poll_duration == poll_duration.zero())
	{
		timeout = 0;
	}
	else if (poll_duration != (poll_duration.max)())
	{
		timeout = static_cast<DWORD>(poll_duration.count());
	}

	// completions since last poll
	impl->notify(process, listener);

	::OVERLAPPED_ENTRY events[max_poll_events];
	ULONG event_count{};

	int result = ::GetQueuedCompletionStatusEx(
		impl->handle,
		events, max_poll_events,
		&event_count,
		timeout,
		false
	);

	if (!result)
	{
		event_count = 0;
	}

	for (auto *event = events;  event != events + event_count;  ++event)
	{
		auto *request = owner_of(*event);
		auto &socket = *reinterpret_cast<socket::impl_type *>(event->lpCompletionKey);

		bytes_transferred = flags = 0;
		result = ::WSAGetOverlappedResult(
			socket.handle,
			event->lpOverlapped,
			&bytes_transferred,
			false,
			&flags
		);

		// BOOL -> int
		if (result == FALSE)
		{
			result = ::WSAGetLastError();
			if (result == WSAEMSGSIZE)
			{
				// unify with Posix
				flags |= MSG_TRUNC;
				result = 0;
			}
			else if (result == WSAENOTSOCK || result == WSA_INVALID_HANDLE)
			{
				result = WSAEBADF;
			}
		}
		else
		{
			result = 0;
		}

		if (result)
		{
			request->error.assign(result, std::system_category());
		}
		else if (auto *receive_from = std::get_if<async::receive_from>(request))
		{
			receive_from->bytes_transferred = bytes_transferred;
			receive_from->flags = flags;
		}
		else if (auto *receive = std::get_if<async::receive>(request))
		{
			receive->bytes_transferred = bytes_transferred;
			receive->flags = flags;
		}
		else if (auto *send_to = std::get_if<async::send_to>(request))
		{
			send_to->bytes_transferred = bytes_transferred;
		}
		else if (auto *send = std::get_if<async::send>(request))
		{
			send->bytes_transferred = bytes_transferred;
		}
		else if (auto *connect = std::get_if<async::connect>(request))
		{
			result = ::setsockopt(
				socket.handle,
				SOL_SOCKET,
				SO_UPDATE_CONNECT_CONTEXT,
				nullptr,
				0
			);
			if (result == 0)
			{
				// unlike other requests, async_connect() does
				// not clear error in advance
				connect->bytes_transferred = bytes_transferred;
				request->error.clear();
			}
			else
			{
				request->error.assign(::WSAGetLastError(), std::system_category());
			}
		}
		process(listener, request);
	}

	// completions since this poll
	impl->notify(process, listener);
}


} // namespace net::__bits


__pal_end
