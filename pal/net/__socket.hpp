#pragma once

/**
 * \file pal/net/__socket.hpp
 * Platform socket abstraction (internal)
 */

#include <pal/buffer.hpp>
#include <pal/result.hpp>
#include <pal/version.hpp>
#include <array>
#include <chrono>
#include <type_traits>

#if __pal_os_linux || __pal_os_macos
	#define __pal_net_posix 1
	#define __pal_net_winsock 0
	#include <poll.h>
	#include <sys/socket.h>
	#include <unistd.h>
#elif __pal_os_windows
	#define __pal_net_posix 0
	#define __pal_net_winsock 1
	#include <winsock2.h>
	#include <mswsock.h>
#endif

namespace pal::net::__socket
{

// NOLINTBEGIN(misc-non-private-member-variables-in-classes)

constexpr size_t io_vector_max_size = 4;

#if __pal_net_posix //{{{1

enum class handle_type : int
{
	invalid = -1,
};

using sa_family = ::sa_family_t;
using timeval = ::timeval;

constexpr std::chrono::milliseconds to_chrono_ms (const timeval &tv) noexcept
{
	using namespace std::chrono;
	return duration_cast<milliseconds>(seconds{tv.tv_sec} + microseconds{tv.tv_usec});
}

constexpr timeval from_chrono_ms (const std::chrono::milliseconds &ms) noexcept
{
	using namespace std::chrono;
	const auto secs = duration_cast<seconds>(ms);
	const auto usecs = duration_cast<microseconds>(ms - secs);
	return {.tv_sec = secs.count(), .tv_usec = static_cast<decltype(timeval::tv_usec)>(usecs.count())};
}

inline pal::unexpected sys_error (int e = errno) noexcept
{
	return pal::unexpected{std::error_code{e, std::generic_category()}};
}

struct message: ::msghdr
{
	std::array<::iovec, io_vector_max_size> iov{};

	template <pal::const_buffer_sequence Buffers>
	bool set (const Buffers &buffers) noexcept
	{
		msg_iovlen = 0;
		msg_iov = iov.data();
		// NOLINTNEXTLINE(readability-use-anyofallof)
		for (auto &&b: buffers)
		{
			if (static_cast<size_t>(msg_iovlen) == iov.size())
			{
				return false;
			}
			iov[msg_iovlen++] = {
				.iov_base = const_cast<void *>(static_cast<const void *>(std::data(b))),
				.iov_len = std::size(b) * sizeof(*std::data(b)),
			};
		}
		return true;
	}

	void reset () noexcept
	{
		msg_iovlen = 0;
	}

	void name (const void *n, size_t name_size) noexcept
	{
		msg_name = const_cast<void *>(n);
		msg_namelen = name_size;
	}

	void flags (int f) noexcept
	{
		msg_flags = f;
	}
};

#elif __pal_net_winsock //{{{1

enum class handle_type : ::SOCKET
{
	invalid = INVALID_SOCKET,
};

using sa_family = ::ADDRESS_FAMILY;
using timeval = ::DWORD;

constexpr std::chrono::milliseconds to_chrono_ms (const timeval &tv) noexcept
{
	return std::chrono::milliseconds{tv};
}

constexpr timeval from_chrono_ms (const std::chrono::milliseconds &ms) noexcept
{
	return static_cast<timeval>(ms.count());
}

inline pal::unexpected sys_error (int e = ::WSAGetLastError()) noexcept
{
	if (e == WSAENOTSOCK || e == WSA_INVALID_HANDLE)
	{
		e = WSAEBADF;
	}
	return pal::unexpected{std::error_code{e, std::system_category()}};
}

struct message
{
	sockaddr *msg_name{};
	INT msg_namelen{};
	DWORD msg_flags{};
	DWORD msg_iovlen{};
	std::array<::WSABUF, io_vector_max_size> msg_iov{};

	template <pal::const_buffer_sequence Buffers>
	bool set (const Buffers &buffers) noexcept
	{
		msg_iovlen = 0;
		for (auto &&b: buffers)
		{
			if (static_cast<size_t>(msg_iovlen) == msg_iov.size())
			{
				return false;
			}
			msg_iov[msg_iovlen++] = {
				.len = static_cast<ULONG>(std::size(b) * sizeof(*std::data(b))),
				.buf = const_cast<CHAR *>(
					static_cast<const CHAR *>(static_cast<const void *>(std::data(b)))
				),
			};
		}
		return true;
	}

	void reset () noexcept
	{
		msg_iovlen = 0;
	}

	void name (const void *n, size_t name_size) noexcept
	{
		msg_name = static_cast<sockaddr *>(const_cast<void *>(n));
		msg_namelen = static_cast<INT>(name_size);
	}

	void flags (int f) noexcept
	{
		msg_flags = f;
	}
};

// clang-format off
#define SHUT_RD SD_RECEIVE
#define SHUT_WR SD_SEND
#define SHUT_RDWR SD_BOTH
// clang-format on

extern ::LPFN_CONNECTEX ConnectEx;
extern ::LPFN_ACCEPTEX AcceptEx;
extern ::LPFN_GETACCEPTEXSOCKADDRS GetAcceptExSockaddrs;

#endif //}}}1

constexpr auto to_sys (handle_type h) noexcept
{
	return static_cast<std::underlying_type_t<handle_type>>(h);
}

constexpr auto from_sys (std::underlying_type_t<handle_type> h) noexcept
{
	return static_cast<handle_type>(h);
}

constexpr int reuse_port =
#ifdef SO_REUSEPORT_LB
	SO_REUSEPORT_LB
#elifdef SO_REUSEPORT
	SO_REUSEPORT
#else
	-1
#endif
	;

enum option_level : int
{
	lib = -1,
};

enum option_name : int
{
	non_blocking_io,
};

// NOLINTEND(misc-non-private-member-variables-in-classes)

} // namespace pal::net::__socket
