#include <pal/net/__socket>
#include <pal/net/socket>

#if __pal_net_posix

namespace pal::net {

const result<void> &init () noexcept
{
	static const result<void> no_error;
	return no_error;
}

result<native_socket> open (int family, int type, int protocol) noexcept
{
	if (auto h = ::socket(family, type, protocol); h != native_socket_handle::invalid)
	{
		return native_socket_handle{h};
	}

	// Library public API deals with Protocol types/instances, translate
	// invalid argument(s) to std::errc::protocol_not_supported
	auto error = errno;
	if constexpr (os == os_type::linux)
	{
		if (error == EINVAL)
		{
			error = EPROTONOSUPPORT;
		}
	}
	else if constexpr (os == os_type::macos)
	{
		if (error == EAFNOSUPPORT)
		{
			error = EPROTONOSUPPORT;
		}
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

result<socket_base::impl_ptr> socket_base::make (native_socket &&handle, int family) noexcept
{
	if (auto socket = new(std::nothrow) impl_type{std::move(handle), family})
	{
		return impl_ptr{socket};
	}
	return make_unexpected(std::errc::not_enough_memory);
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

#endif // __pal_net_posix
