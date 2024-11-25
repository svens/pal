#include <pal/net/__socket>

#if __pal_net_posix

#include <pal/net/socket>

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

result<void> socket_base::bind (const impl_ptr &impl, const void *endpoint, size_t endpoint_size) noexcept
{
	if (::bind(impl->socket->handle, static_cast<const sockaddr *>(endpoint), endpoint_size) == 0)
	{
		return {};
	}
	return __socket::sys_error();
}

result<void> socket_base::local_endpoint (const impl_ptr &impl, void *endpoint, size_t *endpoint_size) noexcept
{
	auto size = static_cast<socklen_t>(*endpoint_size);
	if (::getsockname(impl->socket->handle, static_cast<sockaddr *>(endpoint), &size) == 0)
	{
		*endpoint_size = size;
		return {};
	}
	return __socket::sys_error();
}

result<void> socket_base::get_option (const impl_ptr &impl, int level, int name, void *data, size_t data_size) noexcept
{
	socklen_t size = data_size;
	if (::getsockopt(impl->socket->handle, level, name, data, &size) > -1)
	{
		return {};
	}
	return __socket::sys_error();
}

result<void> socket_base::set_option (const impl_ptr &impl, int level, int name, const void *data, size_t data_size) noexcept
{
	if (::setsockopt(impl->socket->handle, level, name, data, data_size) > -1)
	{
		return {};
	}
	return __socket::sys_error();
}

} // namespace pal::net

#endif // __pal_net_posix
