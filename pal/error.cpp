#include <pal/error>
#include <string>

#include <pal/version>
#if __pal_os_linux || __pal_os_macos
	#include <cerrno>
#elif __pal_os_windows
	#include <windows.h>
#endif

namespace pal {

namespace {

constexpr std::string_view as_view (errc ec) noexcept
{
	switch (ec)
	{
		#define __pal_errc_case(Code, Message) \
			case pal::errc::Code: return Message;
		__pal_errc(__pal_errc_case)
		#undef __pal_errc_case
	};
	return "unknown";
}

} // namespace

const std::error_category &error_category () noexcept
{
	struct error_category_impl: std::error_category
	{
		const char *name () const noexcept final
		{
			return "pal";
		}

		std::string message (int ec) const final
		{
			return std::string{as_view(static_cast<errc>(ec))};
		}
	};
	static const error_category_impl impl{};
	return impl;
}

namespace this_thread {

std::error_code last_system_error () noexcept
{
	#if __pal_os_linux || __pal_os_macos
		return {errno, std::generic_category()};
	#elif __pal_os_windows
		return {static_cast<int>(::GetLastError()), std::system_category()};
	#endif
}

} // namespace this_thread

} // namespace pal
