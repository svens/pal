#include <pal/net/__socket>
#include <pal/net/socket>

#if __pal_net_winsock

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
			return open(AF_INET, SOCK_STREAM)
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

result<native_socket> open (int domain, int type, int protocol) noexcept
{
	if (auto h = ::socket(domain, type, protocol); h != native_socket_handle::invalid)
	{
		return native_socket_handle{h};
	}
	return __socket::sys_error();
}

struct socket_base::impl_type
{
	native_socket socket;
};

void socket_base::impl_type_deleter::operator() (impl_type *impl)
{
	delete impl;
}

const native_socket &socket_base::socket (const impl_ptr &impl) noexcept
{
	return impl->socket;
}

} // namespace pal::net

#endif // __pal_net_winsock
