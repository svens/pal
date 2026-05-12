#pragma once

/**
 * \file pal/net/socket_option.hpp
 * Socket option storage templates
 */

#include <pal/net/__socket.hpp>
#include <chrono>

namespace pal::net
{

/// Socket option storage for type \a T
template <typename T>
struct socket_option_storage
{
	static_assert(std::is_trivially_copyable_v<T>);

	socket_option_storage () noexcept = default;

	constexpr explicit socket_option_storage (T value) noexcept
		: value_{value}
	{
	}

	[[nodiscard]] constexpr T value () const noexcept
	{
		return value_;
	}

	template <typename Self, typename Protocol>
	[[nodiscard]] auto *data (this Self &self, const Protocol &) noexcept
	{
		return std::addressof(self.value_);
	}

	template <typename Protocol>
	[[nodiscard]] constexpr size_t size (const Protocol &) const noexcept
	{
		return sizeof(T);
	}

	template <typename Protocol>
	[[nodiscard]] constexpr result<void> resize (const Protocol &, size_t size) noexcept
	{
		if (size == sizeof(T))
		{
			return {};
		}
		return make_unexpected(std::errc::invalid_argument);
	}

protected:

	T value_{};
};

template <>
struct socket_option_storage<bool>: socket_option_storage<int>
{
	socket_option_storage () noexcept = default;

	constexpr explicit socket_option_storage (bool value) noexcept
		: socket_option_storage<int>{static_cast<int>(value)}
	{
	}

	[[nodiscard]] constexpr bool value () const noexcept
	{
		return value_ != 0;
	}

	constexpr explicit operator bool () const noexcept
	{
		return value();
	}

	constexpr bool operator!() const noexcept
	{
		return !value();
	}

	template <typename Protocol>
	[[nodiscard]] constexpr result<void> resize (const Protocol &, size_t size) noexcept
	{
		if (size == sizeof(int) || size == 1)
		{
			return {};
		}
		return make_unexpected(std::errc::invalid_argument);
	}
};

/// Socket option level (SOL_SOCKET, IPPROTO_TCP, etc)
using socket_option_level = __socket::option_level;

/// Socket option name (SO_BROADCAST, etc)
using socket_option_name = __socket::option_name;

/// Generic socket option \a Name of type \a T on \a Level
template <typename T, socket_option_level Level, socket_option_name Name>
struct socket_option: socket_option_storage<T>
{
	using socket_option_storage<T>::socket_option_storage;

	/// Set new \a value for socket option
	socket_option &operator= (const T &value) noexcept
	{
		socket_option_storage<T>::value_ = value;
		return *this;
	}

	/// Returns socket option level
	template <typename Protocol>
	[[nodiscard]] constexpr socket_option_level level (const Protocol &) const noexcept
	{
		return Level;
	}

	/// Returns socket option name
	template <typename Protocol>
	[[nodiscard]] constexpr socket_option_name name (const Protocol &) const noexcept
	{
		return Name;
	}
};

/// \defgroup socket_option Socket options
/// \{

// clang-format off

/// Non-blocking I/O
using non_blocking_io = socket_option<bool,
	socket_option_level::lib,
	socket_option_name::non_blocking_io
>;

/// Permit sending broadcast messages, if supported by protocol
using broadcast = socket_option<bool,
	socket_option_level{SOL_SOCKET},
	socket_option_name{SO_BROADCAST}
>;

/// Record debugging information by the underlying protocol
using debug = socket_option<bool,
	socket_option_level{SOL_SOCKET},
	socket_option_name{SO_DEBUG}
>;

/// Outgoing messages bypass standard routing facilities
using do_not_route = socket_option<bool,
	socket_option_level{SOL_SOCKET},
	socket_option_name{SO_DONTROUTE}
>;

/// Permit sending keep-alive messages, if supported by protocol
using keepalive = socket_option<bool,
	socket_option_level{SOL_SOCKET},
	socket_option_name{SO_KEEPALIVE}
>;

/// Receive out-of-band data inline
using out_of_band_inline = socket_option<bool,
	socket_option_level{SOL_SOCKET},
	socket_option_name{SO_OOBINLINE}
>;

/// Allow reuse of local endpoints, if supported by protocol
using reuse_address = socket_option<bool,
	socket_option_level{SOL_SOCKET},
	socket_option_name{SO_REUSEADDR}
>;

/// Allow reuse of local port, if supported by protocol
using reuse_port = socket_option<bool,
	socket_option_level{SOL_SOCKET},
	socket_option_name{__socket::reuse_port}
>;

/// Size of receive buffer associated with socket
using receive_buffer_size = socket_option<int,
	socket_option_level{SOL_SOCKET},
	socket_option_name{SO_RCVBUF}
>;

/// Minimum bytes to process for socket input operations
using receive_low_watermark = socket_option<int,
	socket_option_level{SOL_SOCKET},
	socket_option_name{SO_RCVLOWAT}
>;

/// Size of send buffer associated with socket
using send_buffer_size = socket_option<int,
	socket_option_level{SOL_SOCKET},
	socket_option_name{SO_SNDBUF}
>;

/// Minimum bytes to process for socket output operations
using send_low_watermark = socket_option<int,
	socket_option_level{SOL_SOCKET},
	socket_option_name{SO_SNDLOWAT}
>;

/// Controls behaviour when a socket is closed and unsent data is present
struct linger: socket_option<::linger, socket_option_level{SOL_SOCKET}, socket_option_name{SO_LINGER}>
{
	linger () noexcept = default;

	linger &operator= (const ::linger &) = delete;

	constexpr linger (bool e, const std::chrono::seconds &t) noexcept
	{
		enabled(e);
		timeout(t);
	}

	[[nodiscard]] constexpr bool enabled () const noexcept
	{
		return value_.l_onoff != 0;
	}

	constexpr void enabled (bool e) noexcept
	{
		value_.l_onoff = static_cast<decltype(value_.l_onoff)>(e);
	}

	[[nodiscard]] constexpr std::chrono::seconds timeout () const noexcept
	{
		return std::chrono::seconds{value_.l_linger};
	}

	constexpr void timeout (const std::chrono::seconds &t) noexcept
	{
		value_.l_linger = static_cast<decltype(value_.l_linger)>(t.count());
	}
};

/// Timeout for blocking socket I/O calls
template <socket_option_level Level, socket_option_name Name>
struct io_timeout: socket_option<__socket::timeval, Level, Name>
{
	io_timeout () noexcept = default;

	io_timeout &operator= (const __socket::timeval &) = delete;

	constexpr io_timeout (const std::chrono::milliseconds &t) noexcept
	{
		timeout(t);
	}

	[[nodiscard]] constexpr std::chrono::milliseconds timeout () const noexcept
	{
		return __socket::to_chrono_ms(this->value_);
	}

	constexpr void timeout (const std::chrono::milliseconds &t) noexcept
	{
		this->value_ = __socket::from_chrono_ms(t);
	}
};

/// Timeout for blocking socket receive syscalls
using receive_timeout = io_timeout<
	socket_option_level{SOL_SOCKET},
	socket_option_name{SO_RCVTIMEO}
>;

/// Timeout for blocking socket send syscalls
using send_timeout = io_timeout<
	socket_option_level{SOL_SOCKET},
	socket_option_name{SO_SNDTIMEO}
>;

// clang-format on

/// \}

} // namespace pal::net
