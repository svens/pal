#include <pal/net/__socket>
#include <pal/net/socket_base>

#if __pal_net_posix

namespace pal::net {

const result<void> &init () noexcept
{
	static const result<void> no_error;
	return no_error;
}

} // namespace pal::net

#endif // __pal_net_posix
