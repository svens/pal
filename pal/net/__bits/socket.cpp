#include <pal/__bits/platform_sdk>
#include <pal/net/__bits/socket>

#if __pal_os_linux || __pal_os_macos
	#include <unistd.h>
#elif __pal_os_windows
	#include <mutex>
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
		error.assign(::WSAGetLastError(), std::system_category());
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


#endif //}}}1


} // namespace net::__bits


__pal_end
