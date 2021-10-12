#include <pal/net/__bits/socket>

#if __pal_os_linux || __pal_os_macos

#include <pal/net/async/request>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>


__pal_begin


namespace net::__bits {


const result<void> &init () noexcept
{
	static const result<void> no_error;
	return no_error;
}


namespace {

#if !defined(SO_NOSIGPIPE)
	constexpr int SO_NOSIGPIPE = -1;
#endif

constexpr message_flags internal_flags = MSG_DONTWAIT | MSG_NOSIGNAL;

void init_handle (native_socket handle) noexcept
{
	if constexpr (is_macos_build)
	{
		int optval = 1;
		::setsockopt(handle,
			SOL_SOCKET,
			SO_NOSIGPIPE,
			&optval,
			sizeof(optval)
		);
	}
}

unexpected<std::error_code> sys_error (int e = errno) noexcept
{
	return unexpected<std::error_code>{std::in_place, e, std::generic_category()};
}

bool is_blocking_error (int error = errno) noexcept
{
	return error == EAGAIN || error == EWOULDBLOCK;
}

} // namespace


result<void> close_handle (native_socket handle) noexcept
{
	for (;;)
	{
		if (::close(handle) == 0)
		{
			return {};
		}
		else if (errno != EINTR)
		{
			return make_unexpected(static_cast<std::errc>(errno));
		}
	}
}


struct service::impl_type
{
	int handle = -1;
	async::request_queue completed{};

	~impl_type () noexcept
	{
		handle_guard{handle};
	}

	void notify (notify_fn notify, void *listener) noexcept
	{
		auto requests = std::move(completed);
		while (auto *request = requests.try_pop())
		{
			notify(listener, request);
		}
	}
};


struct socket::impl_type
{
	native_socket handle = invalid_native_socket;
	bool is_acceptor{};
	int family{};

	__bits::service::impl_type *service{};
	async::request_queue pending_receive{}, pending_send{};

	void receive_one (async::request *request, size_t &bytes_transferred, message_flags &flags) noexcept;
	void send_one (async::request *request, size_t &bytes_transferred) noexcept;

	void receive_many (service::notify_fn notify, void *listener) noexcept;
	void send_many (service::notify_fn notify, void *listener) noexcept;
	void accept_many (service::notify_fn notify, void *listener) noexcept;

	bool try_now (async::request_queue &queue, async::request *request) noexcept
	{
		if (request->error)
		{
			service->completed.push(request);
			return false;
		}

		request->impl_.message.msg_flags |= internal_flags;
		if (!queue.empty())
		{
			queue.push(request);
			return false;
		}

		return true;
	}

	~impl_type () noexcept
	{
		handle_guard{handle};
		if (service)
		{
			cancel(EBADF);
		}
	}

	void cancel (async::request_queue &queue, int error) noexcept
	{
		while (auto *request = queue.try_pop())
		{
			request->error.assign(error, std::generic_category());
			service->completed.push(request);
		}
	}

	void cancel (int error) noexcept
	{
		cancel(pending_receive, error);
		cancel(pending_send, error);
	}

	void cancel (async::request_queue &queue, service::notify_fn notify, void *listener, int error) noexcept
	{
		while (auto *request = queue.try_pop())
		{
			request->error.assign(error, std::generic_category());
			notify(listener, request);
		}
	}

	void cancel (service::notify_fn notify, void *listener, int error) noexcept
	{
		cancel(pending_receive, notify, listener, error);
		cancel(pending_send, notify, listener, error);
	}

	int pending_error () const noexcept
	{
		int error{};
		socklen_t error_size = sizeof(error);
		if (::getsockopt(handle, SOL_SOCKET, SO_ERROR, &error, &error_size) == -1)
		{
			error = errno;
		}
		return error;
	}

	#if __pal_os_linux

		static constexpr size_t max_mmsg = 64;

		::mmsghdr *make_mmsg (async::request *src, ::mmsghdr *it, ::mmsghdr *end) noexcept
		{
			for (/**/;  src && it != end;  ++it, src = src->next)
			{
				it->msg_hdr = src->impl_.message;
			}
			return it;
		}

		constexpr bool await_read () noexcept
		{
			return true;
		}

		constexpr bool await_write () noexcept
		{
			return true;
		}

	#elif __pal_os_macos

		bool await_read () noexcept;
		bool await_write () noexcept;

	#endif
};


socket::socket (socket &&) noexcept = default;
socket &socket::operator= (socket &&) noexcept = default;
socket::~socket () noexcept = default;


service::service (service &&) noexcept = default;
service &service::operator= (service &&) noexcept = default;
service::~service () noexcept = default;


socket::socket () noexcept
	: impl{}
{ }


socket::socket (impl_ptr impl) noexcept
	: impl{std::move(impl)}
{ }


result<socket> socket::open (bool is_acceptor, int family, int type, int protocol) noexcept
{
	auto errc = std::errc::not_enough_memory;
	if (auto impl = impl_ptr{new(std::nothrow) impl_type})
	{
		impl->handle = ::socket(family, type, protocol);
		if (impl->handle != invalid_native_socket)
		{
			impl->is_acceptor = is_acceptor;
			impl->family = family;
			init_handle(impl->handle);
			return impl;
		}

		// public API deals with Protocol types/instances, translate
		// invalid argument(s) into std::errc::protocol_not_supported
		errc = static_cast<std::errc>(errno);
		if constexpr (is_linux_build)
		{
			if (errc == std::errc::invalid_argument)
			{
				errc = std::errc::protocol_not_supported;
			}
		}
		else if constexpr (is_macos_build)
		{
			if (errc == std::errc::address_family_not_supported)
			{
				errc = std::errc::protocol_not_supported;
			}
		}
	}
	return make_unexpected(errc);
}


result<void> socket::assign (bool is_acceptor, int family, int, int, native_socket handle) noexcept
{
	if (impl)
	{
		impl->is_acceptor = is_acceptor;
		impl->family = family;
		return close_handle(std::exchange(impl->handle, handle));
	}
	else if ((impl = impl_ptr{new(std::nothrow) impl_type}))
	{
		impl->is_acceptor = is_acceptor;
		impl->family = family;
		impl->handle = handle;
		return {};
	}
	return make_unexpected(std::errc::not_enough_memory);
}


result<void> socket::close () noexcept
{
	return close_handle(release());
}


native_socket socket::handle () const noexcept
{
	return impl->handle;
}


int socket::family () const noexcept
{
	return impl->family;
}


native_socket socket::release () noexcept
{
	auto s = std::move(impl);
	return std::exchange(s->handle, invalid_native_socket);
}


result<void> socket::bind (const void *endpoint, size_t endpoint_size) noexcept
{
	if (::bind(impl->handle, static_cast<const sockaddr *>(endpoint), endpoint_size) == 0)
	{
		return {};
	}
	return sys_error();
}


result<void> socket::listen (int backlog) noexcept
{
	if (::listen(impl->handle, backlog) == 0)
	{
		return {};
	}
	return sys_error();
}


result<native_socket> socket::accept (void *endpoint, size_t *endpoint_size) noexcept
{
	socklen_t size = endpoint_size ? *endpoint_size : 0;
	auto h = ::accept(impl->handle, static_cast<sockaddr *>(endpoint), &size);
	if (h > -1)
	{
		init_handle(h);
		if (endpoint_size)
		{
			*endpoint_size = size;
		}
		return h;
	}
	return sys_error();
}


result<void> socket::connect (const void *endpoint, size_t endpoint_size) noexcept
{
	if (::connect(impl->handle, static_cast<const sockaddr *>(endpoint), endpoint_size) == 0)
	{
		return {};
	}
	return sys_error();
}


result<size_t> socket::receive (message &msg) noexcept
{
	auto r = ::recvmsg(impl->handle, &msg, msg.msg_flags | MSG_NOSIGNAL);
	if (r > -1)
	{
		return static_cast<size_t>(r);
	}
	else if (is_blocking_error(errno))
	{
		// unify with Windows
		return sys_error(ETIMEDOUT);
	}
	return sys_error();
}


result<size_t> socket::send (const message &msg) noexcept
{
	auto r = ::sendmsg(impl->handle, &msg, msg.msg_flags | MSG_NOSIGNAL);
	if (r > -1)
	{
		return static_cast<size_t>(r);
	}
	else if (is_blocking_error(errno))
	{
		// unify with Windows
		return sys_error(ETIMEDOUT);
	}
	else if (errno == EDESTADDRREQ)
	{
		// unify with Windows for sendmsg with no recipient endpoint
		// (adjusting Windows for Linux behaviour would be harder,
		// requiring additional syscall)
		return sys_error(ENOTCONN);
	}
	if constexpr (is_linux_build)
	{
		// sendmsg(2) BUGS
		// "Linux may return EPIPE instead of ENOTCONN"
		if (errno == EPIPE)
		{
			return sys_error(ENOTCONN);
		}
	}
	return sys_error();
}


result<size_t> socket::available () const noexcept
{
	int value{};
	if (::ioctl(impl->handle, FIONREAD, &value) > -1)
	{
		return static_cast<size_t>(value);
	}
	return sys_error();
}


result<void> socket::local_endpoint (void *endpoint, size_t *endpoint_size) const noexcept
{
	auto size = static_cast<socklen_t>(*endpoint_size);
	if (::getsockname(impl->handle, static_cast<sockaddr *>(endpoint), &size) == 0)
	{
		*endpoint_size = size;
		return {};
	}
	return sys_error();
}


result<void> socket::remote_endpoint (void *endpoint, size_t *endpoint_size) const noexcept
{
	auto size = static_cast<socklen_t>(*endpoint_size);
	if (::getpeername(impl->handle, static_cast<sockaddr *>(endpoint), &size) == 0)
	{
		*endpoint_size = size;
		return {};
	}
	return sys_error();
}


result<bool> socket::native_non_blocking () const noexcept
{
	auto flags = ::fcntl(impl->handle, F_GETFL, 0);
	if (flags > -1)
	{
		return static_cast<bool>(flags & O_NONBLOCK);
	}
	return sys_error();
}


result<void> socket::native_non_blocking (bool mode) const noexcept
{
	auto flags = ::fcntl(impl->handle, F_GETFL, 0);
	if (flags > -1)
	{
		if (mode)
		{
			flags |= O_NONBLOCK;
		}
		else
		{
			flags &= ~O_NONBLOCK;
		}
		if (::fcntl(impl->handle, F_SETFL, flags) > -1)
		{
			return {};
		}
	}
	return sys_error();
}


result<void> socket::get_option (int level, int name, void *data, size_t data_size) const noexcept
{
	socklen_t size = data_size;
	if (::getsockopt(impl->handle, level, name, data, &size) > -1)
	{
		return {};
	}
	return sys_error();
}


result<void> socket::set_option (int level, int name, const void *data, size_t data_size) noexcept
{
	if (::setsockopt(impl->handle, level, name, data, data_size) > -1)
	{
		return {};
	}
	return sys_error();
}


bool socket::has_async () const noexcept
{
	return impl->service != nullptr;
}


void socket::start (async::receive_from &receive_from) noexcept
{
	auto *request = owner_of(receive_from);
	if (impl->try_now(impl->pending_receive, request))
	{
		impl->receive_one(request, receive_from.bytes_transferred, receive_from.flags);
	}
}


void socket::start (async::receive &receive) noexcept
{
	auto *request = owner_of(receive);
	if (impl->try_now(impl->pending_receive, request))
	{
		impl->receive_one(request, receive.bytes_transferred, receive.flags);
	}
}


void socket::start (async::send_to &send_to) noexcept
{
	auto *request = owner_of(send_to);
	if (impl->try_now(impl->pending_send, request))
	{
		impl->send_one(request, send_to.bytes_transferred);
	}
}


void socket::start (async::send &send) noexcept
{
	auto *request = owner_of(send);
	if (impl->try_now(impl->pending_send, request))
	{
		impl->send_one(request, send.bytes_transferred);
	}
}


void socket::start (async::connect &connect) noexcept
{
	auto *request = owner_of(connect);
	if (impl->try_now(impl->pending_send, request))
	{
		auto r = ::connect(impl->handle,
			static_cast<const sockaddr *>(request->impl_.message.msg_name),
			request->impl_.message.msg_namelen
		);
		if (r > -1)
		{
			if (request->impl_.message.msg_iovlen > 0)
			{
				impl->send_one(request, connect.bytes_transferred);
			}
			else
			{
				impl->service->completed.push(request);
			}
		}
		else if (errno == EINPROGRESS && impl->await_write())
		{
			impl->pending_send.push(request);
		}
		else
		{
			request->error.assign(errno, std::generic_category());
			impl->service->completed.push(request);
		}
	}
}


void socket::start (async::accept &accept) noexcept
{
	auto *request = owner_of(accept);
	if (impl->try_now(impl->pending_receive, request))
	{
		accept.guard_.handle = ::accept(impl->handle,
			static_cast<sockaddr *>(request->impl_.message.msg_name),
			&request->impl_.message.msg_namelen
		);
		if (accept.guard_.handle > -1)
		{
			impl->service->completed.push(request);
		}
		else if (is_blocking_error(errno) && impl->await_read())
		{
			impl->pending_receive.push(request);
		}
		else
		{
			request->error.assign(errno, std::generic_category());
			impl->service->completed.push(request);
		}
	}
}


void socket::impl_type::accept_many (service::notify_fn notify, void *listener) noexcept
{
	while (auto *it = pending_receive.head())
	{
		auto r = ::accept(handle,
			static_cast<sockaddr *>(it->impl_.message.msg_name),
			&it->impl_.message.msg_namelen
		);
		if (r > -1)
		{
			std::get_if<async::accept>(it)->guard_.handle = r;
			notify(listener, pending_receive.pop());
		}
		else
		{
			if (!is_blocking_error(errno) || !await_read())
			{
				it->error.assign(errno, std::generic_category());
				notify(listener, pending_receive.pop());
			}
			break;
		}
	}
}


void socket::impl_type::receive_one (async::request *request,
	size_t &bytes_transferred,
	message_flags &flags) noexcept
{
	auto r = ::recvmsg(handle, &request->impl_.message, request->impl_.message.msg_flags);
	if (r > -1)
	{
		bytes_transferred = r;
		flags = request->impl_.message.msg_flags & ~internal_flags;
	}
	else if (is_blocking_error(errno) && await_read())
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
	else if (is_blocking_error(errno) && await_write())
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


} // namespace net::__bits


__pal_end


#endif // __pal_os_linux || __pal_os_macos


#if __pal_os_linux
	#include "socket_epoll.ipp"
#elif __pal_os_macos
	#include "socket_kqueue.ipp"
#endif
