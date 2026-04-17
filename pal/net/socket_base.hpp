#pragma once

/**
 * \file pal/net/socket_base.hpp
 * Base socket types and values
 */

#include <pal/net/__socket.hpp>
#include <utility>

namespace pal::net
{

/// Initialize networking library. There is no need to call it explicitly as
/// it is also invoked internally by static initialization. Possible exception
/// is when application layer own static initialization order depends on winsock
/// library being loaded. It can be safely called multiple times.
const result<void> &init () noexcept;

/// Owning native socket handle. Closes the OS socket on destruction.
///
/// \note Do not instantiate this class directly, fetch native socket only
/// if library does not provide typesafe wrapper for native OS socket API.
class native_socket
{
public:

	/// Native OS socket handle type
	using handle_type = __socket::handle_type;

	/// Invalid OS socket handle value
	static constexpr handle_type invalid = __socket::invalid_handle;

	native_socket () = default;

	~native_socket () noexcept
	{
		close(handle_);
	}

	/// Construct native_socket from native OS \a handle and address \a family
	// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
	native_socket (handle_type handle, int family) noexcept
		: handle_{handle}
		, family_{family}
	{
	}

	native_socket (native_socket &&that) noexcept
		: handle_{std::exchange(that.handle_, invalid)}
		, family_{that.family_}
	{
	}

	native_socket &operator= (native_socket &&that) noexcept
	{
		close(std::exchange(handle_, std::exchange(that.handle_, invalid)));
		family_ = that.family_;
		return *this;
	}

	/// Returns true if this instance holds a valid OS socket handle
	explicit operator bool () const noexcept
	{
		return handle_ != invalid;
	}

	/// Return native OS socket handle value
	[[nodiscard]] handle_type handle () const noexcept
	{
		return handle_;
	}

	/// Return address family of this socket
	[[nodiscard]] int family () const noexcept
	{
		return family_;
	}

	/// Release ownership of the native OS socket handle, returning its value
	[[nodiscard]] handle_type release () noexcept
	{
		return std::exchange(handle_, invalid);
	}

	static void close (handle_type h) noexcept;

	[[nodiscard]] result<void> bind (const void *endpoint, size_t endpoint_size) const noexcept;
	[[nodiscard]] result<void> listen (int backlog) const noexcept;
	[[nodiscard]] result<native_socket> accept (void *endpoint, size_t *endpoint_size) const noexcept;
	[[nodiscard]] result<void> connect (const void *endpoint, size_t endpoint_size) const noexcept;
	[[nodiscard]] result<void> shutdown (int what) const noexcept;

	[[nodiscard]] result<size_t> send (const __socket::message &message) const noexcept;
	[[nodiscard]] result<size_t> receive (__socket::message &message) const noexcept;

	[[nodiscard]] result<size_t> available () const noexcept;
	[[nodiscard]] result<void> local_endpoint (void *endpoint, size_t *endpoint_size) const noexcept;
	[[nodiscard]] result<void> remote_endpoint (void *endpoint, size_t *endpoint_size) const noexcept;

	[[nodiscard]] result<void> get_option (int level, int name, void *data, size_t data_size) const noexcept;
	[[nodiscard]] result<void> set_option (int level, int name, const void *data, size_t data_size) const noexcept;

private:

	handle_type handle_ = invalid;
	int family_ = 0;
};

/// Open and return new native socket
[[nodiscard]] result<native_socket> open (int family, int type, int protocol) noexcept;

/// Common socket types and constants
class socket_base
{
public:

	/// Native socket handle type
	using native_handle_type = native_socket::handle_type;

	/// Bitmask flags for send/receive operations
	enum message_flags : int
	{
		/// Peek at incoming message, without removing it from queue
		message_peek = MSG_PEEK,

		/// Process out-of-band data
		message_out_of_band = MSG_OOB,

		/// Bypass routing, use direct interface
		message_do_not_route = MSG_DONTROUTE,

		/// Received message was truncated because receive buffer was too small
		message_truncated = MSG_TRUNC,
	};

	/// Socket state change waiting types
	enum wait_type : int
	{
		/// Wait for socket to become readable
		wait_read = POLLIN,

		/// Wait for socket to become writable
		wait_write = POLLOUT,

		/// Wait until socket has pending error condition
		wait_error = POLLERR,
	};

	/// Socket shutdown types
	enum shutdown_type : int
	{
		/// Disable further receive operations
		shutdown_receive = SHUT_RD,

		/// Disable further send operations
		shutdown_send = SHUT_WR,

		/// Disable further send/receive operations
		shutdown_both = SHUT_RDWR,
	};

	/// Limit on length of queue of pending incoming connections
	static constexpr int max_listen_connections = SOMAXCONN;

	/// Maximum number of spans for vectored I/O
	static constexpr size_t io_vector_max_size = __socket::io_vector_max_size;

	/// \internal
	///
	/// Return lambda that wraps socket implementation to public API class
	template <typename Socket>
	static constexpr auto to_api () noexcept
	{
		return [] (auto &&socket)
		{
			return Socket{std::forward<decltype(socket)>(socket)};
		};
	}

protected:

	~socket_base () = default;
};

} // namespace pal::net
