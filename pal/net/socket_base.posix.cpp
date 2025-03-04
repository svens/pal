#include <pal/net/__socket>

#if __pal_net_posix

#include <pal/net/socket>
#include <fcntl.h>
#include <sys/ioctl.h>

namespace pal::net {

const result<void> &init () noexcept
{
	static const result<void> no_error;
	return no_error;
}

result<native_socket> open (int family, int type, int protocol) noexcept
{
	if (auto h = ::socket(family, type, protocol); h != native_socket_handle::invalid)
	{
		return native_socket_handle{h, family};
	}

	// Library public API deals with Protocol types/instances, translate
	// invalid argument(s) to std::errc::protocol_not_supported
	auto error = errno;
	if constexpr (os == os_type::linux)
	{
		if (error == EINVAL)
		{
			error = EPROTONOSUPPORT;
		}
	}
	else if constexpr (os == os_type::macos)
	{
		if (error == EAFNOSUPPORT)
		{
			error = EPROTONOSUPPORT;
		}
	}

	return __socket::sys_error(error);
}

void native_socket_handle::close::operator() (pointer socket) const noexcept
{
	while (true)
	{
		if (::close(socket->handle) == 0 || errno != EINTR)
		{
			return;
		}
	}
}

result<void> native_socket_handle::bind (const void *endpoint, size_t endpoint_size) const noexcept
{
	if (::bind(handle, static_cast<const sockaddr *>(endpoint), endpoint_size) == 0)
	{
		return {};
	}
	return __socket::sys_error();
}

result<void> native_socket_handle::listen (int backlog) const noexcept
{
	if (::listen(handle, backlog) == 0)
	{
		return {};
	}
	return __socket::sys_error();
}

namespace {

#if !defined(SO_NOSIGPIPE)
	constexpr int SO_NOSIGPIPE = -1;
#endif

__socket::handle_type init (__socket::handle_type h) noexcept
{
	if constexpr (SO_NOSIGPIPE != -1)
	{
		int optval = 1;
		::setsockopt(h, SOL_SOCKET, SO_NOSIGPIPE, &optval, sizeof(optval));
	}
	return h;
}

constexpr bool is_blocking_error (int error) noexcept
{
	if constexpr (EAGAIN != EWOULDBLOCK)
	{
		return error == EAGAIN || error == EWOULDBLOCK;
	}
	else
	{
		return error == EAGAIN;
	}
}

constexpr bool is_connection_error (int error) noexcept
{
	return error == EDESTADDRREQ || error == EPIPE;
}

} // namespace

result<native_socket_handle> native_socket_handle::accept (void *endpoint, size_t *endpoint_size) const noexcept
{
	socklen_t size = endpoint_size ? *endpoint_size : 0;
	auto h = ::accept(handle, static_cast<sockaddr *>(endpoint), &size);
	if (h > invalid)
	{
		if (endpoint_size)
		{
			*endpoint_size = size;
		}
		return native_socket_handle{init(h), family};
	}
	return __socket::sys_error();
}

result<void> native_socket_handle::connect (const void *endpoint, size_t endpoint_size) const noexcept
{
	if (::connect(handle, static_cast<const sockaddr *>(endpoint), endpoint_size) == 0)
	{
		return {};
	}
	return __socket::sys_error();
}

result<void> native_socket_handle::shutdown (int what) const noexcept
{
	if (::shutdown(handle, what) == 0)
	{
		return {};
	}
	return __socket::sys_error();
}

result<size_t> native_socket_handle::send (const __socket::message &message) noexcept
{
	if (auto r = ::sendmsg(handle, &message, message.msg_flags | MSG_NOSIGNAL); r > -1)
	{
		return static_cast<size_t>(r);
	}
	else if (is_blocking_error(errno))
	{
		// unify with Windows
		return __socket::sys_error(ETIMEDOUT);
	}
	else if (is_connection_error(errno))
	{
		// unify with Windows for sendmsg() with no recipient
		// (adjusting Windows for Linux would be more expensive, requiring additional syscall
		return __socket::sys_error(ENOTCONN);
	}
	return __socket::sys_error();
}

result<size_t> native_socket_handle::receive (__socket::message &message) noexcept
{
	if (auto r = ::recvmsg(handle, &message, message.msg_flags | MSG_NOSIGNAL); r > -1)
	{
		return static_cast<size_t>(r);
	}
	else if (is_blocking_error(errno))
	{
		// unify with Windows
		return __socket::sys_error(ETIMEDOUT);
	}
	return __socket::sys_error();
}

result<size_t> native_socket_handle::available () const noexcept
{
	int value{};
	if (::ioctl(handle, FIONREAD, &value) > -1)
	{
		return static_cast<size_t>(value);
	}
	return __socket::sys_error();
}

result<void> native_socket_handle::local_endpoint (void *endpoint, size_t *endpoint_size) const noexcept
{
	auto size = static_cast<socklen_t>(*endpoint_size);
	if (::getsockname(handle, static_cast<sockaddr *>(endpoint), &size) == 0)
	{
		*endpoint_size = size;
		return {};
	}
	return __socket::sys_error();
}

result<void> native_socket_handle::remote_endpoint (void *endpoint, size_t *endpoint_size) const noexcept
{
	auto size = static_cast<socklen_t>(*endpoint_size);
	if (::getpeername(handle, static_cast<sockaddr *>(endpoint), &size) == 0)
	{
		*endpoint_size = size;
		return {};
	}
	return __socket::sys_error();
}

namespace {

result<void> get_native_non_blocking (__socket::handle_type handle, int &mode) noexcept
{
	auto flags = ::fcntl(handle, F_GETFL, 0);
	if (flags > -1)
	{
		mode = (flags & O_NONBLOCK) == O_NONBLOCK;
		return {};
	}
	return __socket::sys_error();
}

result<void> set_native_non_blocking (__socket::handle_type handle, int mode) noexcept
{
	auto flags = ::fcntl(handle, F_GETFL, 0);
	if (flags > -1)
	{
		if (mode != 0)
		{
			flags |= O_NONBLOCK;
		}
		else
		{
			flags &= ~O_NONBLOCK;
		}
		if (::fcntl(handle, F_SETFL, flags) > -1)
		{
			return {};
		}
	}
	return __socket::sys_error();
}

} // namespace

result<void> native_socket_handle::get_option (int level, int name, void *data, size_t data_size) const noexcept
{
	if (level == __socket::option_level::lib)
	{
		switch (name)
		{
			case __socket::option_name::non_blocking_io:
				return get_native_non_blocking(handle, *static_cast<int *>(data));
		}
	}

	socklen_t size = data_size;
	if (::getsockopt(handle, level, name, data, &size) > -1)
	{
		return {};
	}
	return __socket::sys_error();
}

result<void> native_socket_handle::set_option (int level, int name, const void *data, size_t data_size) const noexcept
{
	if (level == __socket::option_level::lib)
	{
		switch (name)
		{
			case __socket::option_name::non_blocking_io:
				return set_native_non_blocking(handle, *static_cast<const int *>(data));
		}
	}

	if (::setsockopt(handle, level, name, data, data_size) > -1)
	{
		return {};
	}
	return __socket::sys_error();
}

} // namespace pal::net

#endif // __pal_net_posix
