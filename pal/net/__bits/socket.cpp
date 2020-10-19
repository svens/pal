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

bool validate_socket_option (native_socket handle, int name,
	std::error_code &error) noexcept
{
	if (handle != invalid_native_socket && name > -1)
	{
		return true;
	}
	error.assign(
		handle == invalid_native_socket ? EBADF : ENOPROTOOPT,
		std::system_category()
	);
	return false;
}

inline void init_socket (native_socket handle) noexcept
{
	#if !defined(SO_NOSIGPIPE)
		constexpr int SO_NOSIGPIPE = -1;
	#endif

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

} // namespace


const std::error_code &init () noexcept
{
	static const std::error_code no_error{};
	return no_error;
}


socket socket::open (int domain, int type, int protocol, std::error_code &error) noexcept
{
	auto handle = call(::socket, error, domain, type, protocol);
	if (handle != invalid_native_socket)
	{
		init_socket(handle);
	}
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


void socket::listen (int backlog, std::error_code &error) noexcept
{
	call(::listen, error, handle, backlog);
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


native_socket socket::accept (
	void *endpoint,
	size_t *endpoint_size,
	std::error_code &error) noexcept
{
	do
	{
		auto new_handle = ::accept(handle,
			static_cast<sockaddr *>(endpoint),
			reinterpret_cast<socklen_t *>(endpoint_size)
		);
		if (new_handle != invalid_native_socket)
		{
			init_socket(new_handle);
			error.clear();
			return new_handle;
		}
	} while (errno == ECONNABORTED && !flags.enable_connection_aborted);

	return check_result(invalid_native_socket, error);
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
		call(::fcntl, error, handle, F_SETFL, flags);
	}
}


size_t socket::get_option (
	int level,
	int name,
	void *data,
	size_t data_size,
	std::error_code &error) const noexcept
{
	if (validate_socket_option(handle, name, error))
	{
		socklen_t size = data_size;
		call(::getsockopt, error, handle, level, name, data, &size);
		return size;
	}
	return {};
}


void socket::set_option (
	int level,
	int name,
	const void *data,
	size_t data_size,
	std::error_code &error) noexcept
{
	if (validate_socket_option(handle, name, error))
	{
		call(::setsockopt, error, handle, level, name, data, data_size);
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

bool validate_socket_option (native_socket handle, int name,
	std::error_code &error) noexcept
{
	if (handle != invalid_native_socket && name > -1)
	{
		return true;
	}
	error.assign(
		handle == invalid_native_socket ? WSAEBADF : WSAENOPROTOOPT,
		std::system_category()
	);
	return false;
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


void socket::listen (int backlog, std::error_code &error) noexcept
{
	call(::listen, error, handle, backlog);
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


native_socket socket::accept (
	void *endpoint,
	size_t *endpoint_size,
	std::error_code &error) noexcept
{
	do
	{
		auto new_handle = ::accept(handle,
			static_cast<sockaddr *>(endpoint),
			reinterpret_cast<socklen_t *>(endpoint_size)
		);
		if (new_handle != invalid_native_socket)
		{
			error.clear();
			return new_handle;
		}
	} while (::WSAGetLastError() == WSAECONNABORTED && !flags.enable_connection_aborted);

	return check_result(invalid_native_socket, error);
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
	error.assign(
		handle == invalid_native_socket ? WSAEBADF : WSAEOPNOTSUPP,
		std::system_category()
	);
	return {};
}


void socket::set_native_non_blocking (bool mode, std::error_code &error) noexcept
{
	unsigned long arg = mode ? 1 : 0;
	call(::ioctlsocket, error, handle, FIONBIO, &arg);
}


size_t socket::get_option (
	int level,
	int name,
	void *data,
	size_t data_size,
	std::error_code &error) const noexcept
{
	if (validate_socket_option(handle, name, error))
	{
		auto size = static_cast<socklen_t>(data_size);
		call(::getsockopt,
			error,
			handle,
			level,
			name,
			static_cast<char *>(data),
			&size
		);
		if (data_size == sizeof(INT) && size == sizeof(bool))
		{
			size = sizeof(INT);
		}
		return size;
	}
	return {};
}


void socket::set_option (
	int level,
	int name,
	const void *data,
	size_t data_size,
	std::error_code &error) noexcept
{
	if (validate_socket_option(handle, name, error))
	{
		call(::setsockopt,
			error,
			handle,
			level,
			name,
			static_cast<const char *>(data),
			static_cast<int>(data_size)
		);
	}
}


#endif //}}}1


const char *host_name (std::error_code &error) noexcept
{
	static char buf[256] = { '\0' };
	call(::gethostname, error, buf, sizeof(buf) - 1);
	return buf;
}


} // namespace net::__bits


__pal_end
