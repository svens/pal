#include <pal/net/__socket>

#if __pal_net_winsock

#include <pal/net/socket>
#include <ws2tcpip.h>

namespace pal::net {

::LPFN_CONNECTEX ConnectEx{};
::LPFN_ACCEPTEX AcceptEx{};
::LPFN_GETACCEPTEXSOCKADDRS GetAcceptExSockaddrs{};

const result<void> &init () noexcept
{
	struct lib_init
	{
		result<void> status;

		lib_init () noexcept
			: status{load()}
		{ }

		~lib_init () noexcept
		{
			::WSACleanup();
		}

		static result<void> load_wsa () noexcept
		{
			::WSADATA wsa{};
			if (auto r = ::WSAStartup(MAKEWORD(2, 2), &wsa))
			{
				return __socket::sys_error(r);
			}
			return {};
		}

		static result<native_socket> load_ex_impl (void *fn, GUID id, native_socket &&socket) noexcept
		{
			DWORD bytes{};
			auto r = ::WSAIoctl(socket->handle,
				SIO_GET_EXTENSION_FUNCTION_POINTER,
				&id,
				sizeof(id),
				fn,
				sizeof(fn),
				&bytes,
				nullptr,
				nullptr
			);
			if (r != SOCKET_ERROR)
			{
				return socket;
			}
			return __socket::sys_error();
		}

		static auto load_ex (void *fn, GUID id) noexcept
		{
			return [=](native_socket &&socket)
			{
				return load_ex_impl(fn, id, std::move(socket));
			};
		}

		static result<void> load_mswsock_extensions () noexcept
		{
			return open(AF_INET, SOCK_STREAM, 0)
				.and_then(load_ex(&ConnectEx, WSAID_CONNECTEX))
				.and_then(load_ex(&AcceptEx, WSAID_ACCEPTEX))
				.and_then(load_ex(&GetAcceptExSockaddrs, WSAID_GETACCEPTEXSOCKADDRS))
				.transform([](auto &&){ /* result<socket> -> result<void> */ })
			;
		}

		static result<void> load () noexcept
		{
			return load_wsa().and_then(load_mswsock_extensions);
		}
	};

	static const lib_init lib_init;
	return lib_init.status;
}

namespace {

const auto &lib_init = init();

} // namespace

result<native_socket> open (int family, int type, int protocol) noexcept
{
	if (auto h = ::socket(family, type, protocol); h != native_socket_handle::invalid)
	{
		return native_socket_handle{h, family};
	}

	// Library public API deals with Protocol types/instances, translate
	// invalid argument(s) to std::errc::protocol_not_supported
	auto error = ::WSAGetLastError();
	if (error == WSAESOCKTNOSUPPORT)
	{
		error = WSAEPROTONOSUPPORT;
	}

	return __socket::sys_error(error);
}

void native_socket_handle::close::operator() (pointer socket) const noexcept
{
	::closesocket(socket->handle);
}

result<void> native_socket_handle::bind (const void *endpoint, size_t endpoint_size) const noexcept
{
	if (::bind(handle, static_cast<const sockaddr *>(endpoint), static_cast<int>(endpoint_size)) == 0)
	{
		return {};
	}
	return __socket::sys_error();
}

namespace {

bool make_any (int family, void *endpoint, int *endpoint_size) noexcept
{
	int size = 0;
	if (family == AF_INET)
	{
		size = sizeof(::sockaddr_in);
	}
	else if (family == AF_INET6)
	{
		size = sizeof(::sockaddr_in6);
	}

	if (size && size <= *endpoint_size)
	{
		*endpoint_size = size;
		std::fill_n(static_cast<std::byte *>(endpoint), size, std::byte{});
		static_cast<sockaddr *>(endpoint)->sa_family = static_cast<__socket::sa_family>(family);
		return true;
	}

	return false;
}

__socket::handle_type init (__socket::handle_type h, int type) noexcept
{
	::SetFileCompletionNotificationModes(
		reinterpret_cast<::HANDLE>(h),
		FILE_SKIP_COMPLETION_PORT_ON_SUCCESS |
		FILE_SKIP_SET_EVENT_ON_HANDLE
	);

	if (type == SOCK_DGRAM)
	{
		bool new_behaviour = false;
		DWORD ignored;
		::WSAIoctl(
			h,
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

	return h;
}

} // namespace

result<void> native_socket_handle::listen (int backlog) const noexcept
{
	int e = 0;

	// If socket is not bound yet:
	// - Posix: it is done automatically
	// - Windows: returns error. Align with Posix -- bind and retry listen once

	for (auto i = 0; i < 2; ++i)
	{
		if (::listen(handle, backlog) == 0)
		{
			return {};
		}

		e = ::WSAGetLastError();
		if (e == WSAEINVAL)
		{
			sockaddr_storage ss{};
			int ss_size = sizeof(ss);
			if (!make_any(family, &ss, &ss_size))
			{
				break;
			}
			else if (::bind(handle, reinterpret_cast<sockaddr *>(&ss), ss_size) == 0)
			{
				continue;
			}
		}

		break;
	}

	return __socket::sys_error(e);
}

result<native_socket_handle> native_socket_handle::accept (void *endpoint, size_t *endpoint_size) const noexcept
{
	int size = 0, *size_p = nullptr;
	if (endpoint_size)
	{
		size = static_cast<int>(*endpoint_size);
		size_p = &size;
	}

	auto h = ::accept(handle, static_cast<sockaddr *>(endpoint), size_p);
	if (h != invalid)
	{
		if (endpoint_size)
		{
			*endpoint_size = size;
		}
		return native_socket_handle{init(h, SOCK_STREAM), family};
	}
	return __socket::sys_error();
}

result<void> native_socket_handle::connect (const void *endpoint, size_t endpoint_size) const noexcept
{
	if (::connect(handle, static_cast<const sockaddr *>(endpoint), static_cast<int>(endpoint_size)) == 0)
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

result<void> native_socket_handle::local_endpoint (void *endpoint, size_t *endpoint_size) const noexcept
{
	auto size = static_cast<int>(*endpoint_size);
	if (::getsockname(handle, static_cast<sockaddr *>(endpoint), &size) == 0)
	{
		*endpoint_size = size;
		return {};
	}

	// querying unbound socket name returns WSAEINVAL
	// align with Posix API that succeeds with family-specific "any"
	auto e = ::WSAGetLastError();
	if (e == WSAEINVAL)
	{
		if (make_any(family, endpoint, &size))
		{
			*endpoint_size = size;
			return {};
		}
	}

	return __socket::sys_error(e);
}

result<void> native_socket_handle::remote_endpoint (void *endpoint, size_t *endpoint_size) const noexcept
{
	auto size = static_cast<int>(*endpoint_size);
	if (::getpeername(handle, static_cast<sockaddr *>(endpoint), &size) == 0)
	{
		*endpoint_size = size;
		return {};
	}
	return __socket::sys_error();
}

namespace {

int socket_option_precheck (__socket::handle_type handle, int name) noexcept
{
	if (handle == native_socket_handle::invalid)
	{
		return WSAEBADF;
	}
	else if (name == -1)
	{
		return WSAENOPROTOOPT;
	}
	return 0;
}

result<void> get_native_non_blocking (__socket::handle_type, int &) noexcept
{
	return __socket::sys_error(WSAEOPNOTSUPP);
}

result<void> set_native_non_blocking (__socket::handle_type handle, bool mode) noexcept
{
	unsigned long arg = mode ? 1 : 0;
	if (::ioctlsocket(handle, FIONBIO, &arg) == 0)
	{
		return {};
	}
	return __socket::sys_error();
}

} // namespace

result<void> native_socket_handle::get_option (int level, int name, void *data, size_t data_size) const noexcept
{
	if (auto e = socket_option_precheck(handle, name))
	{
		return __socket::sys_error(e);
	}
	else if (level == __socket::option_level::lib)
	{
		switch (name)
		{
			case __socket::option_name::non_blocking_io:
				return get_native_non_blocking(handle, *static_cast<int *>(data));
		}
	}

	auto p = static_cast<char *>(data);
	auto size = static_cast<int>(data_size);
	if (::getsockopt(handle, level, name, p, &size) > -1)
	{
		return {};
	}

	return __socket::sys_error();
}

result<void> native_socket_handle::set_option (int level, int name, const void *data, size_t data_size) const noexcept
{
	if (auto e = socket_option_precheck(handle, name))
	{
		return __socket::sys_error(e);
	}
	else if (level == __socket::option_level::lib)
	{
		switch (name)
		{
			case __socket::option_name::non_blocking_io:
				return set_native_non_blocking(handle, *static_cast<const int *>(data));
		}
	}

	auto p = static_cast<const char *>(data);
	auto size = static_cast<int>(data_size);
	if (::setsockopt(handle, level, name, p, size) > -1)
	{
		return {};
	}
	return __socket::sys_error();
}

} // namespace pal::net

#endif // __pal_net_winsock
