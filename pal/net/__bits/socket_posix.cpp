#include <pal/net/__bits/socket>

#if __pal_os_linux || __pal_os_macos

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

} // namespace


struct socket::impl_type
{
	native_socket handle = invalid_native_socket;

	~impl_type () noexcept
	{
		if (handle != invalid_native_socket)
		{
			(void)close_handle(handle);
		}
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


result<socket> socket::open (int domain, int type, int protocol) noexcept
{
	auto errc = std::errc::not_enough_memory;
	if (auto impl = impl_ptr{new(std::nothrow) impl_type})
	{
		impl->handle = ::socket(domain, type, protocol);
		if (impl->handle != invalid_native_socket)
		{
			init_handle(impl->handle);
			return impl;
		}

		// public API deals with Protocol types/instances, translate
		// invalid argument(s) into std::errc::protocol_not_supported
		if constexpr (is_linux_build)
		{
			if (errno == EINVAL)
			{
				errno = EPROTONOSUPPORT;
			}
		}
		else if constexpr (is_macos_build)
		{
			if (errno == EAFNOSUPPORT)
			{
				errno = EPROTONOSUPPORT;
			}
		}
		errc = static_cast<std::errc>(errno);
	}
	return make_unexpected(errc);
}


result<void> socket::assign (int, int, int, native_socket handle) noexcept
{
	if (impl)
	{
		return close_handle(std::exchange(impl->handle, handle));
	}
	else if ((impl = impl_ptr{new(std::nothrow) impl_type}))
	{
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


native_socket socket::release () noexcept
{
	auto s = std::move(impl);
	return std::exchange(s->handle, invalid_native_socket);
}


} // namespace net::__bits


__pal_end


#endif // __pal_os_linux || __pal_os_macos
