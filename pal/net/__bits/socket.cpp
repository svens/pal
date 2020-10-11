#include <pal/__bits/platform_sdk>
#include <pal/net/__bits/socket>

#if __pal_os_linux || __pal_os_macos
	#include <fcntl.h>
	#include <poll.h>
	#include <sys/ioctl.h>
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
			flags.native_non_blocking = -1;
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


wait_type socket::wait (wait_type what, int timeout_ms, std::error_code &error) noexcept
{
	if (handle == invalid_native_socket)
	{
		error.assign(EBADF, std::generic_category());
		return {};
	}

	pollfd fd{};
	fd.fd = handle;
	fd.events = what;
	auto event_count = ::poll(&fd, 1, timeout_ms);
	if (event_count > 0)
	{
		fd.revents &= (POLLIN | POLLOUT | POLLERR);
		return static_cast<wait_type>(fd.revents);
	}

	check_result(event_count, error);
	return {};
}


void socket::shutdown (shutdown_type what, std::error_code &error) noexcept
{
	call(::shutdown, error, handle, static_cast<int>(what));
}


void socket::local_endpoint (
	int /*family*/,
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


size_t socket::available (std::error_code &error) const noexcept
{
	unsigned long value{};
	call(::ioctl, error, handle, FIONREAD, &value);
	return value;
}


bool socket::get_native_non_blocking (std::error_code &error) const noexcept
{
	return O_NONBLOCK & call(::fcntl, error, handle, F_GETFL, 0);
}


void socket::set_native_non_blocking (bool mode, std::error_code &error) noexcept
{
	int flags = call(::fcntl, error, handle, F_GETFL, 0);
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
		call(::fcntl, error, handle, F_SETFL, 0);
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
	flags.native_non_blocking = -1;
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


wait_type socket::wait (wait_type what, int timeout_ms, std::error_code &error) noexcept
{
	WSAPOLLFD fd{};
	fd.fd = handle;
	fd.events = static_cast<SHORT>(what);
	auto event_count = ::WSAPoll(&fd, 1, timeout_ms);
	if (event_count > 0)
	{
		fd.revents &= (POLLIN | POLLOUT | POLLERR);
		return static_cast<wait_type>(fd.revents);
	}
	check_result(event_count, error);
	return {};
}


void socket::shutdown (shutdown_type what, std::error_code &error) noexcept
{
	call(::shutdown, error, handle, static_cast<int>(what));
}


namespace {

bool set_family_specific_any (int family,
	void *endpoint,
	socklen_t *endpoint_size) noexcept
{
	socklen_t size = 0;
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
	static_cast<sockaddr *>(endpoint)->sa_family =
		static_cast<sa_family>(family);

	return true;
}

} // namespace


void socket::local_endpoint (
	int family,
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
		// On Windows querying unbound socket name returns error.
		// Align with Linux/MacOS that return any
		if (set_family_specific_any(family, endpoint, &size))
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


size_t socket::available (std::error_code &error) const noexcept
{
  unsigned long value{};
  call(::ioctlsocket, error, handle, FIONBIO, &value);
  return value;
}


bool socket::get_native_non_blocking (std::error_code &error) const noexcept
{
  error.assign(WSAEOPNOTSUPP, std::system_category());
  return {};
}


void socket::set_native_non_blocking (bool mode, std::error_code &error) noexcept
{
	unsigned long arg = mode ? 1 : 0;
	call(::ioctlsocket, error, handle, FIONBIO, &arg);
}


#endif //}}}1


} // namespace net::__bits


__pal_end
