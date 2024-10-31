#include <pal/net/__socket>
#include <pal/net/socket>

#if __pal_net_posix

namespace pal::net {

const result<void> &init () noexcept
{
	static const result<void> no_error;
	return no_error;
}

namespace __socket {

result<handle> make (int domain, int type, int protocol) noexcept
{
	if (auto value = ::socket(domain, type, protocol); value != native_handle::invalid)
	{
		return native_handle{value};
	}
	return sys_error();
}

} // namespace __socket

} // namespace pal::net

#endif // __pal_net_posix
