#include <pal/net/__socket>
#include <pal/net/socket>

#if __pal_net_posix

namespace pal::net {

const result<void> &init () noexcept
{
	static const result<void> no_error;
	return no_error;
}

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

#endif // __pal_net_posix
