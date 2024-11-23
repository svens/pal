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

result<native_socket> open (int domain, int type, int protocol) noexcept
{
	if (auto h = ::socket(domain, type, protocol); h != native_socket_handle::invalid)
	{
		return native_socket_handle{h};
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

struct socket_base::impl_type
{
	native_socket socket;
	int family;

	impl_type (native_socket &&socket, int family) noexcept
		: socket{std::move(socket)}
		, family{family}
	{ }
};

void socket_base::impl_type_deleter::operator() (impl_type *impl)
{
	delete impl;
}

result<socket_base::impl_ptr> socket_base::open (int family, int type, int protocol) noexcept
{
	return net::open(family, type, protocol).and_then([=](auto &&handle) -> result<impl_ptr>
	{
		if (auto socket = new(std::nothrow) impl_type{std::move(handle), family})
		{
			return impl_ptr{socket};
		}
		return make_unexpected(std::errc::not_enough_memory);
	});
}

native_socket socket_base::release (impl_ptr &&impl) noexcept
{
	auto s = std::move(impl);
	return std::move(s->socket);
}

const native_socket &socket_base::socket (const impl_ptr &impl) noexcept
{
	return impl->socket;
}

int socket_base::family (const impl_ptr &impl) noexcept
{
	return impl->family;
}

} // namespace pal::net

#endif // __pal_net_winsock
