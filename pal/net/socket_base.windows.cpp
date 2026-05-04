#include <pal/net/__socket.hpp>

#if __pal_net_winsock

// clang-format off
#include <pal/net/socket_base.hpp>
#include <ws2tcpip.h>
// clang-format on

namespace pal::net
{

using __socket::from_sys;
using __socket::to_sys;

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
		{
		}

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
			auto r = ::WSAIoctl(
				to_sys(socket.handle()),
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
				return std::move(socket);
			}
			return __socket::sys_error();
		}

		static auto load_ex (void *fn, GUID id) noexcept
		{
			return [=] (native_socket &&socket)
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
				.transform([] (auto &&) { /* result<native_socket> -> result<void> */ });
		}

		static result<void> load () noexcept
		{
			return load_wsa().and_then(load_mswsock_extensions);
		}
	};

	static const lib_init lib_init;
	return lib_init.status;
}

namespace
{

const auto &lib_init = init();

} // namespace

result<native_socket> open (int family, int type, int protocol) noexcept
{
	if (auto h = from_sys(::socket(family, type, protocol)); h != __socket::handle_type::invalid)
	{
		return native_socket{h, family};
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

void native_socket::close (handle_type h) noexcept
{
	::closesocket(to_sys(h));
}

result<void> native_socket::bind (const void *endpoint, size_t endpoint_size) const noexcept
{
	if (::bind(to_sys(handle_), static_cast<const sockaddr *>(endpoint), static_cast<int>(endpoint_size)) == 0)
	{
		return {};
	}
	return __socket::sys_error();
}

namespace
{

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
		reinterpret_cast<::HANDLE>(to_sys(h)),
		FILE_SKIP_COMPLETION_PORT_ON_SUCCESS | FILE_SKIP_SET_EVENT_ON_HANDLE
	);

	if (type == SOCK_DGRAM)
	{
		bool new_behaviour = false;
		DWORD ignored;
		::WSAIoctl(
			to_sys(h),
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

result<void> native_socket::listen (int backlog) const noexcept
{
	int e = 0;

	// If socket is not bound yet:
	// - Posix: it is done automatically
	// - Windows: returns error. Align with Posix -- bind and retry listen once

	for (auto i = 0; i < 2; ++i)
	{
		if (::listen(to_sys(handle_), backlog) == 0)
		{
			return {};
		}

		e = ::WSAGetLastError();
		if (e == WSAEINVAL)
		{
			sockaddr_storage ss{};
			int ss_size = sizeof(ss);
			if (!make_any(family_, &ss, &ss_size))
			{
				break;
			}
			else if (::bind(to_sys(handle_), reinterpret_cast<sockaddr *>(&ss), ss_size) == 0)
			{
				continue;
			}
		}

		break;
	}

	return __socket::sys_error(e);
}

result<native_socket> native_socket::accept (void *endpoint, size_t *endpoint_size) const noexcept
{
	int size = 0, *size_p = nullptr;
	if (endpoint_size != nullptr)
	{
		size = static_cast<int>(*endpoint_size);
		size_p = &size;
	}

	auto h = from_sys(::accept(to_sys(handle_), static_cast<sockaddr *>(endpoint), size_p));
	if (h != handle_type::invalid)
	{
		if (endpoint_size != nullptr)
		{
			*endpoint_size = size;
		}
		return native_socket{init(h, SOCK_STREAM), family_};
	}
	return __socket::sys_error();
}

result<void> native_socket::connect (const void *endpoint, size_t endpoint_size) const noexcept
{
	if (::connect(to_sys(handle_), static_cast<const sockaddr *>(endpoint), static_cast<int>(endpoint_size)) == 0)
	{
		return {};
	}
	return __socket::sys_error();
}

result<void> native_socket::shutdown (int what) const noexcept
{
	if (::shutdown(to_sys(handle_), what) == 0)
	{
		return {};
	}
	return __socket::sys_error();
}

result<size_t> native_socket::send (const __socket::message &message) const noexcept
{
	int result;
	DWORD sent = 0;

	if (message.msg_name)
	{
		result = ::WSASendTo(
			to_sys(handle_),
			const_cast<WSABUF *>(message.msg_iov.data()),
			message.msg_iovlen,
			&sent,
			message.msg_flags,
			message.msg_name,
			message.msg_namelen,
			nullptr,
			nullptr
		);
	}
	else
	{
		result = ::WSASend(
			to_sys(handle_),
			const_cast<WSABUF *>(message.msg_iov.data()),
			message.msg_iovlen,
			&sent,
			message.msg_flags,
			nullptr,
			nullptr
		);
	}

	if (result != SOCKET_ERROR)
	{
		return sent;
	}

	auto e = ::WSAGetLastError();
	if (e == WSAESHUTDOWN)
	{
		// unify with Posix
		e = WSAENOTCONN;
	}
	return __socket::sys_error(e);
}

result<size_t> native_socket::receive (__socket::message &message) const noexcept
{
	int result;
	DWORD received = 0;

	if (message.msg_name)
	{
		result = ::WSARecvFrom(
			to_sys(handle_),
			message.msg_iov.data(),
			message.msg_iovlen,
			&received,
			&message.msg_flags,
			message.msg_name,
			&message.msg_namelen,
			nullptr,
			nullptr
		);
	}
	else
	{
		result = ::WSARecv(
			to_sys(handle_),
			message.msg_iov.data(),
			message.msg_iovlen,
			&received,
			&message.msg_flags,
			nullptr,
			nullptr
		);
	}

	if (result != SOCKET_ERROR)
	{
		return received;
	}

	auto e = ::WSAGetLastError();
	if (e == WSAESHUTDOWN)
	{
		// unify with Posix
		return 0u;
	}
	return __socket::sys_error(e);
}

result<size_t> native_socket::available () const noexcept
{
	unsigned long value{};
	if (::ioctlsocket(to_sys(handle_), FIONREAD, &value) > -1)
	{
		return value;
	}
	return __socket::sys_error();
}

result<void> native_socket::local_endpoint (void *endpoint, size_t *endpoint_size) const noexcept
{
	auto size = static_cast<int>(*endpoint_size);
	if (::getsockname(to_sys(handle_), static_cast<sockaddr *>(endpoint), &size) == 0)
	{
		*endpoint_size = size;
		return {};
	}

	// querying unbound socket name returns WSAEINVAL
	// align with Posix API that succeeds with family-specific "any"
	auto e = ::WSAGetLastError();
	if (e == WSAEINVAL)
	{
		if (make_any(family_, endpoint, &size))
		{
			*endpoint_size = size;
			return {};
		}
	}

	return __socket::sys_error(e);
}

result<void> native_socket::remote_endpoint (void *endpoint, size_t *endpoint_size) const noexcept
{
	auto size = static_cast<int>(*endpoint_size);
	if (::getpeername(to_sys(handle_), static_cast<sockaddr *>(endpoint), &size) == 0)
	{
		*endpoint_size = size;
		return {};
	}
	return __socket::sys_error();
}

namespace
{

int socket_option_precheck (__socket::handle_type handle, int name) noexcept
{
	if (handle == __socket::handle_type::invalid)
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
	if (::ioctlsocket(to_sys(handle), FIONBIO, &arg) == 0)
	{
		return {};
	}
	return __socket::sys_error();
}

} // namespace

result<void> native_socket::get_option (int level, int name, void *data, size_t &data_size) const noexcept
{
	if (auto e = socket_option_precheck(handle_, name))
	{
		return __socket::sys_error(e);
	}
	else if (level == __socket::option_level::lib)
	{
		switch (name)
		{
			case __socket::option_name::non_blocking_io:
				return get_native_non_blocking(handle_, *static_cast<int *>(data));
			default:
				break;
		}
	}

	auto p = static_cast<char *>(data);
	auto size = static_cast<int>(data_size);
	if (::getsockopt(to_sys(handle_), level, name, p, &size) > -1)
	{
		data_size = size;
		return {};
	}

	return __socket::sys_error();
}

result<void> native_socket::set_option (int level, int name, const void *data, size_t data_size) const noexcept
{
	if (auto e = socket_option_precheck(handle_, name))
	{
		return __socket::sys_error(e);
	}
	else if (level == __socket::option_level::lib)
	{
		switch (name)
		{
			case __socket::option_name::non_blocking_io:
				return set_native_non_blocking(handle_, *static_cast<const int *>(data));
			default:
				break;
		}
	}

	auto p = static_cast<const char *>(data);
	auto size = static_cast<int>(data_size);
	if (::setsockopt(to_sys(handle_), level, name, p, size) > -1)
	{
		return {};
	}
	return __socket::sys_error();
}

} // namespace pal::net

#endif // __pal_net_winsock
