#include <pal/net/__bits/socket>


#if __pal_os_linux || __pal_os_macos


__pal_begin


namespace net::__bits {


const result<void> &init () noexcept
{
	static const result<void> no_error;
	return no_error;
}


struct socket::impl_type
{ };


socket::socket () noexcept
	: impl{}
{ }


socket::~socket () noexcept = default;


} // namespace net::__bits


__pal_end


#endif // __pal_os_linux || __pal_os_macos
