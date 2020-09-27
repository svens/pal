#include <pal/__bits/platform_sdk>
#include <pal/net/__bits/socket>

#if __pal_os_windows
	#include <mutex>
#endif


__pal_begin


namespace net::__bits {


#if __pal_os_linux || __pal_os_macos //{{{1


const std::error_code &init () noexcept
{
	static const std::error_code no_error{};
	return no_error;
}


#elif __pal_os_windows //{{{1


const std::error_code &init () noexcept
{
	static std::error_code init_result;
	static std::once_flag once;
	std::call_once(once,
		[]()
		{
			WSADATA wsa;
			init_result.assign(
				WSAStartup(MAKEWORD(2, 2), &wsa),
				std::system_category()
			);
		}
	);
	return init_result;
}


namespace {


struct lib
{
	lib () noexcept
	{
		init();
	}

	~lib ()
	{
		::WSACleanup();
	}

	static lib instance;
};

lib lib::instance{};


} // namespace


#endif //}}}1


} // namespace net::__bits


__pal_end
