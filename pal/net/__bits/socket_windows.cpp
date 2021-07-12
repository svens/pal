#include <pal/net/__bits/socket>


#if __pal_os_windows

#include <algorithm>
#include <ws2tcpip.h>


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


bool make_family_specific_any (int family, void *endpoint, int *endpoint_size) noexcept
{
	int size = 0;
	if (family == AF_INET)
	{
		size = sizeof(sockaddr_in);
	}
	else if (family == AF_INET6)
	{
		size = sizeof(sockaddr_in6);
	}

	if (!size || size > *endpoint_size)
	{
		return false;
	}

	*endpoint_size = size;
	std::fill_n(static_cast<std::byte *>(endpoint), size, std::byte{});
	static_cast<sockaddr *>(endpoint)->sa_family = static_cast<sa_family>(family);
	return true;
}


int socket_option_precheck (native_socket handle, int name) noexcept
{
	if (handle == invalid_native_socket)
	{
		return WSAEBADF;
	}
	else if (name == -1)
	{
		return WSAENOPROTOOPT;
	}
	return 0;
}


} // namespace


const result<void> &init () noexcept
{
	static const result<void> result = lib::init();
	return result;
}


result<void> close_handle (native_socket handle) noexcept
{
	if (::closesocket(handle) == 0)
	{
		return {};
	}
	return sys_error();
}


struct socket::impl_type
{
	native_socket handle = invalid_native_socket;
	int family;

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
	auto e = ERROR_NOT_ENOUGH_MEMORY;
	if (auto impl = impl_ptr{new(std::nothrow) impl_type})
	{
		impl->handle = ::WSASocketW(family, type, protocol, nullptr, 0, WSA_FLAG_OVERLAPPED);
		if (impl->handle != invalid_native_socket)
		{
			impl->family = family;
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
	if (::bind(impl->handle, static_cast<const sockaddr *>(endpoint), static_cast<int>(endpoint_size)) == 0)
	{
		return {};
	}
	return sys_error();
}


result<void> socket::listen (int backlog) noexcept
{
	int e = 0;

	// Posix: if socket is not bound yet, it is done automatically
	// Windows: returns error. Align with Posix: bind and retry listen once

	for (auto i = 0;  i < 2;  ++i)
	{
		if (::listen(impl->handle, backlog) == 0)
		{
			return {};
		}

		e = ::WSAGetLastError();
		if (e == WSAEINVAL)
		{
			sockaddr_storage ss{};
			int ss_size = sizeof(ss);
			if (!make_family_specific_any(impl->family, &ss, &ss_size))
			{
				break;
			}
			else if (::bind(impl->handle, reinterpret_cast<sockaddr *>(&ss), ss_size) == 0)
			{
				continue;
			}
		}

		break;
	}

	return sys_error();
}


result<native_socket> socket::accept (void *endpoint, size_t *endpoint_size) noexcept
{
	int size = 0, *size_p = nullptr;
	if (endpoint_size)
	{
		size = static_cast<int>(*endpoint_size);
		size_p = &size;
	}

	auto h = ::accept(impl->handle, static_cast<sockaddr *>(endpoint), size_p);
	if (h != invalid_native_socket)
	{
		init_handle(h, SOCK_STREAM);
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
	if (::connect(impl->handle, static_cast<const sockaddr *>(endpoint), static_cast<int>(endpoint_size)) == 0)
	{
		return {};
	}
	return sys_error();
}


result<size_t> socket::receive (message &msg) noexcept
{
	int result;
	DWORD received = 0;
	if (msg.msg_name)
	{
		result = ::WSARecvFrom(
			impl->handle,
			msg.msg_iov.data(),
			msg.msg_iovlen,
			&received,
			&msg.msg_flags,
			msg.msg_name,
			&msg.msg_namelen,
			nullptr,
			nullptr
		);
	}
	else
	{
		result = ::WSARecv(
			impl->handle,
			msg.msg_iov.data(),
			msg.msg_iovlen,
			&received,
			&msg.msg_flags,
			nullptr,
			nullptr
		);
	}
	if (result != SOCKET_ERROR)
	{
		return received;
	}
	return sys_error();
}


result<size_t> socket::send (const message &msg) noexcept
{
	int result;
	DWORD sent = 0;
	if (msg.msg_name)
	{
		result = ::WSASendTo(
			impl->handle,
			const_cast<WSABUF *>(msg.msg_iov.data()),
			msg.msg_iovlen,
			&sent,
			msg.msg_flags,
			msg.msg_name,
			msg.msg_namelen,
			nullptr,
			nullptr
		);
	}
	else
	{
		result = ::WSASend(
			impl->handle,
			const_cast<WSABUF *>(msg.msg_iov.data()),
			msg.msg_iovlen,
			&sent,
			msg.msg_flags,
			nullptr,
			nullptr
		);
	}
	if (result != SOCKET_ERROR)
	{
		return sent;
	}
	return sys_error();
}


result<size_t> socket::available () const noexcept
{
	unsigned long value{};
	if (::ioctlsocket(impl->handle, FIONREAD, &value) > -1)
	{
		return value;
	}
	return sys_error();
}


result<void> socket::local_endpoint (void *endpoint, size_t *endpoint_size) const noexcept
{
	auto size = static_cast<int>(*endpoint_size);
	if (::getsockname(impl->handle, static_cast<sockaddr *>(endpoint), &size) == 0)
	{
		*endpoint_size = size;
		return {};
	}

	// querying unbound socket name returns error WSAEINVAL
	// align with Posix API that succeeds with family-specific "any"
	auto e = ::WSAGetLastError();
	if (e == WSAEINVAL)
	{
		if (make_family_specific_any(impl->family, endpoint, &size))
		{
			*endpoint_size = size;
			return {};
		}
	}

	return sys_error(e);
}


result<void> socket::remote_endpoint (void *endpoint, size_t *endpoint_size) const noexcept
{
	auto size = static_cast<int>(*endpoint_size);
	if (::getpeername(impl->handle, static_cast<sockaddr *>(endpoint), &size) == 0)
	{
		*endpoint_size = size;
		return {};
	}
	return sys_error();
}


result<bool> socket::native_non_blocking () const noexcept
{
	return sys_error(WSAEOPNOTSUPP);
}


result<void> socket::native_non_blocking (bool mode) const noexcept
{
	unsigned long arg = mode ? 1 : 0;
	if (::ioctlsocket(impl->handle, FIONBIO, &arg) == 0)
	{
		return {};
	}
	return sys_error();
}


result<void> socket::get_option (int level, int name, void *data, size_t data_size) const noexcept
{
	if (auto e = socket_option_precheck(impl->handle, name))
	{
		return sys_error(e);
	}

	auto p = static_cast<char *>(data);
	auto size = static_cast<int>(data_size);
	if (::getsockopt(impl->handle, level, name, p, &size) > -1)
	{
		return {};
	}

	return sys_error();
}


result<void> socket::set_option (int level, int name, const void *data, size_t data_size) noexcept
{
	if (auto e = socket_option_precheck(impl->handle, name))
	{
		return sys_error(e);
	}

	auto p = static_cast<const char *>(data);
	auto size = static_cast<int>(data_size);
	if (::setsockopt(impl->handle, level, name, p, size) > -1)
	{
		return {};
	}

	return sys_error();
}


} // namespace net::__bits


__pal_end


#endif // __pal_os_windows
