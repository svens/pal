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
	if (auto h = ::socket(domain, type, protocol); h != __socket::invalid_handle)
	{
		return native_socket_handle{h};
	}
	return __socket::sys_error();
}

} // namespace pal::net

#endif // __pal_net_posix
