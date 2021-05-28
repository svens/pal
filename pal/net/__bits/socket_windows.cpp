#include <pal/net/__bits/socket>


#if __pal_os_windows


__pal_begin


namespace net::__bits {


::LPFN_CONNECTEX ConnectEx{};
::LPFN_ACCEPTEX AcceptEx{};
::LPFN_GETACCEPTEXSOCKADDRS GetAcceptExSockaddrs{};


namespace {


template <typename F>
bool load (F *fn, GUID id, SOCKET socket, std::error_code &error) noexcept
{
	DWORD bytes{};
	auto result = ::WSAIoctl(socket,
		SIO_GET_EXTENSION_FUNCTION_POINTER,
		&id,
		sizeof(id),
		fn,
		sizeof(fn),
		&bytes,
		nullptr,
		nullptr
	);
	if (result != SOCKET_ERROR)
	{
		return true;
	}
	error.assign(::WSAGetLastError(), std::system_category());
	return false;
}


std::error_code init_mswsock_extensions () noexcept
{
	auto s = ::socket(AF_INET, SOCK_STREAM, 0);

	std::error_code result;
	true
		&& load(&ConnectEx, WSAID_CONNECTEX, s, result)
		&& load(&AcceptEx, WSAID_ACCEPTEX, s, result)
		&& load(&GetAcceptExSockaddrs, WSAID_GETACCEPTEXSOCKADDRS, s, result)
	;

	(void)::closesocket(s);
	return result;
}


struct lib
{
	lib () noexcept
	{
		__bits::init();
	}

	~lib () noexcept
	{
		::WSACleanup();
	}

	static result<void> init () noexcept
	{
		::WSADATA wsa{};
		if (auto r = ::WSAStartup(MAKEWORD(2, 2), &wsa))
		{
			return unexpected{std::error_code(r, std::system_category())};
		}
		else if (auto e = init_mswsock_extensions())
		{
			return unexpected{e};
		}
		return {};
	}

	static lib instance;
};
lib lib::instance{};


} // namespace


const result<void> &init () noexcept
{
	static const result<void> result = lib::init();
	return result;
}


struct socket::impl_type
{ };


socket::socket () noexcept
	: impl{}
{ }


socket::~socket () noexcept = default;


} // namespace net::__bits


__pal_end


#endif // __pal_os_windows
