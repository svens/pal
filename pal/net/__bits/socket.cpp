#include <pal/__bits/platform_sdk>
#include <pal/net/__bits/socket>

#if __pal_os_linux || __pal_os_macos
	#include <unistd.h>
#elif __pal_os_windows
	#include <mutex>
	#include <ws2tcpip.h>
#endif


__pal_begin


namespace net::__bits {


#define call(func, error, ...) check_result(func(__VA_ARGS__), error)


#if __pal_os_linux || __pal_os_macos //{{{1


namespace {

template <typename T>
inline T check_result (T result, std::error_code &error) noexcept
{
	if (result != -1)
	{
		error.clear();
	}
	else
	{
		error.assign(errno, std::generic_category());
	}
	return result;
}

} // namespace


const std::error_code &init () noexcept
{
	static const std::error_code no_error{};
	return no_error;
}


socket socket::open (int domain, int type, int protocol, std::error_code &error) noexcept
{
	auto handle = call(::socket, error, domain, type, protocol);

#if __pal_os_macos
	if (handle != invalid_native_socket)
	{
		int optval = 1;
		::setsockopt(handle, SOL_SOCKET, SO_NOSIGPIPE, &optval, sizeof(optval));
	}
#endif

	return {handle};
}


void socket::close (std::error_code &error) noexcept
{
	for (;;)
	{
		if (call(::close, error, handle) == 0 || errno != EINTR)
		{
			handle = invalid_native_socket;
			return;
		}
	}
}


void socket::bind (const void *endpoint, size_t endpoint_size, std::error_code &error) noexcept
{
	call(::bind,
		error,
		handle,
		static_cast<const sockaddr *>(endpoint),
		endpoint_size
	);
}


void socket::connect (
	const void *endpoint,
	size_t endpoint_size,
	std::error_code &error) noexcept
{
	call(::connect,
		error,
		handle,
		static_cast<const sockaddr *>(endpoint),
		endpoint_size
	);
}


void socket::local_endpoint (
	void *endpoint,
	size_t *endpoint_size,
	std::error_code &error) const noexcept
{
	auto size = static_cast<socklen_t>(*endpoint_size);
	auto result = call(::getsockname,
		error,
		handle,
		static_cast<sockaddr *>(endpoint),
		&size
	);
	if (result != -1)
	{
		*endpoint_size = size;
	}
}


void socket::remote_endpoint (
	void *endpoint,
	size_t *endpoint_size,
	std::error_code &error) const noexcept
{
	auto size = static_cast<socklen_t>(*endpoint_size);
	auto result = call(::getpeername,
		error,
		handle,
		static_cast<sockaddr *>(endpoint),
		&size
	);
	if (result != -1)
	{
		*endpoint_size = size;
	}
}


#elif __pal_os_windows //{{{1


namespace {

template <typename T>
inline T check_result (T result, std::error_code &error) noexcept
{
	if (result != SOCKET_ERROR)
	{
		error.clear();
	}
	else
	{
		auto sys_errc = ::WSAGetLastError();
		if (sys_errc == WSAENOTSOCK)
		{
			sys_errc = WSAEBADF;
		}
		error.assign(sys_errc, std::system_category());
	}
	return result;
}

} // namespace


const std::error_code &init () noexcept
{
	static std::error_code init_result;
	static std::once_flag once;
	std::call_once(once,
		[]()
		{
			WSADATA wsa;
			init_result.assign(
				WSAStartup(MAKEWORD(2, 2), &wsa),
				std::system_category()
			);
		}
	);
	return init_result;
}


namespace {


struct lib
{
	lib () noexcept
	{
		init();
	}

	~lib ()
	{
		::WSACleanup();
	}

	static lib instance;
};

lib lib::instance{};


} // namespace


socket socket::open (int domain, int type, int protocol, std::error_code &error) noexcept
{
	auto handle = call(::WSASocketW,
		error,
		domain,
		type,
		protocol,
		nullptr,
		0,
		WSA_FLAG_OVERLAPPED
	);

	if (handle != invalid_native_socket)
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
			::WSAIoctl(handle,
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

	return {handle};
}


void socket::close (std::error_code &error) noexcept
{
	call(::closesocket, error, handle);
	handle = invalid_native_socket;
}


void socket::bind (const void *endpoint, size_t endpoint_size, std::error_code &error) noexcept
{
	call(::bind,
		error,
		handle,
		static_cast<const sockaddr *>(endpoint),
		static_cast<socklen_t>(endpoint_size)
	);
}


void socket::connect (
	const void *endpoint,
	size_t endpoint_size,
	std::error_code &error) noexcept
{
	call(::connect,
		error,
		handle,
		static_cast<const sockaddr *>(endpoint),
		static_cast<socklen_t>(endpoint_size)
	);
}


namespace {

sa_family socket_address_family (native_socket handle) noexcept
{
	WSAPROTOCOL_INFOW info;
	int info_size = sizeof(info);

	auto result = ::getsockopt(handle,
		SOL_SOCKET,
		SO_PROTOCOL_INFO,
		reinterpret_cast<char *>(&info),
		&info_size
	);

	return static_cast<sa_family>(
		result != SOCKET_ERROR ? info.iAddressFamily : AF_UNSPEC
	);
}

bool set_family_specific_any (native_socket handle,
	void *endpoint,
	socklen_t *endpoint_size) noexcept
{
	socklen_t size = 0;
	auto family = socket_address_family(handle);
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
	std::memset(endpoint, 0, size);
	static_cast<sockaddr *>(endpoint)->sa_family = family;

	return true;
}

} // namespace


void socket::local_endpoint (
	void *endpoint,
	size_t *endpoint_size,
	std::error_code &error) const noexcept
{
	auto size = static_cast<socklen_t>(*endpoint_size);
	auto result = call(::getsockname,
		error,
		handle,
		static_cast<sockaddr *>(endpoint),
		&size
	);

	if (error == std::errc::invalid_argument)
	{
		// expensive but necessary to keep similar to Linux/MacOS
		if (set_family_specific_any(handle, endpoint, &size))
		{
			result = ~SOCKET_ERROR;
		}
	}

	if (result != SOCKET_ERROR)
	{
		error.clear();
		*endpoint_size = size;
	}
}


void socket::remote_endpoint (
	void *endpoint,
	size_t *endpoint_size,
	std::error_code &error) const noexcept
{
	auto size = static_cast<socklen_t>(*endpoint_size);
	auto result = call(::getpeername,
		error,
		handle,
		static_cast<sockaddr *>(endpoint),
		&size
	);
	if (result != -1)
	{
		*endpoint_size = size;
	}
}


#endif //}}}1


} // namespace net::__bits


__pal_end
