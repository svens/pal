#include <pal/net/ip/host_name.hpp>
#include <pal/net/__socket.hpp>

namespace pal::net::ip
{

result<std::string_view> host_name () noexcept
{
	static const auto name = [] () noexcept -> result<std::string_view>
	{
		// NOLINTNEXTLINE(modernize-avoid-c-arrays,readability-magic-numbers)
		static char buf[256] = {};
		if (::gethostname(buf, sizeof(buf) - 1) == 0)
		{
			return buf;
		}
		return __socket::sys_error();
	}();
	return name;
}

} // namespace pal::net::ip
