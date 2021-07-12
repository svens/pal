#include <pal/net/__bits/socket>

#if __pal_os_linux || __pal_os_macos

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


struct socket::impl_type
{
	native_socket handle = invalid_native_socket;
	int family{};

	~impl_type () noexcept
	{
		handle_guard{handle};
	}
};


socket::socket (socket &&) noexcept = default;
socket &socket::operator= (socket &&) noexcept = default;
socket::~socket () noexcept = default;


socket::socket () noexcept
	: impl{}
{ }


socket::socket (impl_ptr impl) noexcept
	: impl{std::move(impl)}
{ }


result<socket> socket::open (int family, int type, int protocol) noexcept
{
	auto errc = std::errc::not_enough_memory;
	if (auto impl = impl_ptr{new(std::nothrow) impl_type})
	{
		impl->handle = ::socket(family, type, protocol);
		if (impl->handle != invalid_native_socket)
		{
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


result<void> socket::assign (int family, int, int, native_socket handle) noexcept
{
	if (impl)
	{
		impl->family = family;
		return close_handle(std::exchange(impl->handle, handle));
	}
	else if ((impl = impl_ptr{new(std::nothrow) impl_type}))
	{
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
	else if (errno == EAGAIN)
	{
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
	else if (errno == EAGAIN)
	{
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


} // namespace net::__bits


__pal_end


#endif // __pal_os_linux || __pal_os_macos
