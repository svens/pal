#include <pal/net/__bits/socket>


#if __pal_os_windows


__pal_begin


namespace net::__bits {


::LPFN_CONNECTEX ConnectEx{};
::LPFN_ACCEPTEX AcceptEx{};
::LPFN_GETACCEPTEXSOCKADDRS GetAcceptExSockaddrs{};


namespace {


template <typename F>
bool load (F *fn, GUID id, SOCKET socket, std::error_code &error) noexcept
{
	DWORD bytes{};
	auto result = ::WSAIoctl(socket,
		SIO_GET_EXTENSION_FUNCTION_POINTER,
		&id,
		sizeof(id),
		fn,
		sizeof(fn),
		&bytes,
		nullptr,
		nullptr
	);
	if (result != SOCKET_ERROR)
	{
		return true;
	}
	error.assign(::WSAGetLastError(), std::system_category());
	return false;
}


std::error_code init_mswsock_extensions () noexcept
{
	auto s = ::socket(AF_INET, SOCK_STREAM, 0);

	std::error_code result;
	true
		&& load(&ConnectEx, WSAID_CONNECTEX, s, result)
		&& load(&AcceptEx, WSAID_ACCEPTEX, s, result)
		&& load(&GetAcceptExSockaddrs, WSAID_GETACCEPTEXSOCKADDRS, s, result)
	;

	(void)::closesocket(s);
	return result;
}


struct lib
{
	lib () noexcept
	{
		__bits::init();
	}

	~lib () noexcept
	{
		::WSACleanup();
	}

	static result<void> init () noexcept
	{
		::WSADATA wsa{};
		if (auto r = ::WSAStartup(MAKEWORD(2, 2), &wsa))
		{
			return unexpected{std::error_code(r, std::system_category())};
		}
		else if (auto e = init_mswsock_extensions())
		{
			return unexpected{e};
		}
		return {};
	}

	static lib instance;
};
lib lib::instance{};


inline unexpected<std::error_code> sys_error (int e = ::WSAGetLastError()) noexcept
{
	if (e == WSAENOTSOCK)
	{
		// unify with POSIX
		e = WSAEBADF;
	}
	return unexpected<std::error_code>{std::in_place, e, std::system_category()};
}


void init_handle (native_socket handle, int type) noexcept
{
	::SetFileCompletionNotificationModes(
		reinterpret_cast<HANDLE>(handle),
		FILE_SKIP_COMPLETION_PORT_ON_SUCCESS |
		FILE_SKIP_SET_EVENT_ON_HANDLE
	);

	if (type == SOCK_DGRAM)
	{
		bool new_behaviour = false;
		DWORD ignored;
		::WSAIoctl(
			handle,
			SIO_UDP_CONNRESET,
			&new_behaviour,
			sizeof(new_behaviour),
			nullptr,
			0,
			&ignored,
			nullptr,
			nullptr
		);
	}
}


result<void> close_handle (native_socket handle) noexcept
{
	if (::closesocket(handle) == 0)
	{
		return {};
	}
	return sys_error();
}


} // namespace


const result<void> &init () noexcept
{
	static const result<void> result = lib::init();
	return result;
}


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
	auto e = ERROR_NOT_ENOUGH_MEMORY;
	if (auto impl = impl_ptr{new(std::nothrow) impl_type})
	{
		impl->handle = ::WSASocketW(domain, type, protocol, nullptr, 0, WSA_FLAG_OVERLAPPED);
		if (impl->handle != invalid_native_socket)
		{
			init_handle(impl->handle, type);
			return impl;
		}

		e = ::WSAGetLastError();
		if (e == WSAESOCKTNOSUPPORT)
		{
			// public API deals with Protocol types/instances, translate
			// invalid argument(s) into std::errc::protocol_not_supported
			e = WSAEPROTONOSUPPORT;
		}
	}
	return sys_error(e);
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


#endif // __pal_os_windows
