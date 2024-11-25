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
		return native_socket_handle{h, family};
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

void native_socket_handle::close::operator() (pointer socket) const noexcept
{
	while (true)
	{
		if (::close(socket->handle) == 0 || errno != EINTR)
		{
			return;
		}
	}
}

result<void> native_socket_handle::bind (const void *endpoint, size_t endpoint_size) const noexcept
{
	if (::bind(handle, static_cast<const sockaddr *>(endpoint), endpoint_size) == 0)
	{
		return {};
	}
	return __socket::sys_error();
}

result<void> native_socket_handle::local_endpoint (void *endpoint, size_t *endpoint_size) const noexcept
{
	auto size = static_cast<socklen_t>(*endpoint_size);
	if (::getsockname(handle, static_cast<sockaddr *>(endpoint), &size) == 0)
	{
		*endpoint_size = size;
		return {};
	}
	return __socket::sys_error();
}

result<void> native_socket_handle::get_option (int level, int name, void *data, size_t data_size) const noexcept
{
	socklen_t size = data_size;
	if (::getsockopt(handle, level, name, data, &size) > -1)
	{
		return {};
	}
	return __socket::sys_error();
}

result<void> native_socket_handle::set_option (int level, int name, const void *data, size_t data_size) const noexcept
{
	if (::setsockopt(handle, level, name, data, data_size) > -1)
	{
		return {};
	}
	return __socket::sys_error();
}

} // namespace pal::net

#endif // __pal_net_posix
