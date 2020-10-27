#include <pal/net/error>
#include <string>


__pal_begin


namespace net {


namespace {

constexpr std::string_view as_view (socket_errc ec) noexcept
{
	switch (ec)
	{
		#define __pal_net_socket_errc_case(Code, Message) \
			case pal::net::socket_errc::Code: return Message;
		__pal_net_socket_errc(__pal_net_socket_errc_case)
		#undef __pal_net_socket_errc_case
	};
	return "unknown";
}

} // namespace


const std::error_category &socket_category () noexcept
{
	struct socket_category_impl: std::error_category
	{
		const char *name () const noexcept final
		{
			return "socket";
		}

		std::string message (int ec) const final
		{
			return std::string{as_view(static_cast<socket_errc>(ec))};
		}
	};
	static const socket_category_impl impl{};
	return impl;
}


} // namespace net


__pal_end
