#include <pal/net/ip/host_name>
#include <pal/net/__socket>

namespace pal::net::ip {

namespace {

result<std::string_view> get_host_name () noexcept
{
	static char buf[256] = {};
	if (::gethostname(buf, sizeof(buf) - 1) == 0)
	{
		return buf;
	}
	return __socket::sys_error();
}

} // namespace

result<std::string_view> host_name () noexcept
{
	static const auto name = get_host_name();
	return name;
}

} // namespace pal::net::ip
