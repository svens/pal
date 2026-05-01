#pragma once

/**
 * \file pal/net/basic_socket.hpp
 * Generic socket base class
 */

#include <pal/net/concepts.hpp>
#include <pal/net/socket_base.hpp>

namespace pal::net
{

/// Base class for protocol-specific stream and datagram sockets. Provides
/// common operations: bind, connect, shutdown, endpoint queries, and socket
/// options. \a Protocol must satisfy pal::net::protocol and provide an
/// \c endpoint nested type.
template <typename Protocol>
class basic_socket: public socket_base
{
public:

	/// Socket protocol type
	using protocol_type = Protocol;

	/// Protocol-specific endpoint type
	using endpoint_type = Protocol::endpoint;

	basic_socket (basic_socket &&) = default;
	basic_socket &operator= (basic_socket &&) = default;

	/// Return true if socket holds a valid OS handle
	explicit operator bool () const noexcept
	{
		return socket_.operator bool();
	}

	/// Return socket protocol
	[[nodiscard]] protocol_type protocol () const noexcept
	{
		return protocol_type{socket_.family()};
	}

	/// Release ownership and return the native socket
	[[nodiscard]] net::native_socket release () noexcept
	{
		return std::move(socket_);
	}

	/// Return reference to the native socket
	[[nodiscard]] const net::native_socket &native_socket () const noexcept
	{
		return socket_;
	}

	/// Bind socket to local \a endpoint
	[[nodiscard]] result<void> bind (const endpoint_type &endpoint) noexcept
	{
		return socket_.bind(endpoint.data(), endpoint.size());
	}

	/// Connect socket to remote \a endpoint
	[[nodiscard]] result<void> connect (const endpoint_type &endpoint) noexcept
	{
		return socket_.connect(endpoint.data(), endpoint.size());
	}

	/// Shut down all or part of the full-duplex connection
	[[nodiscard]] result<void> shutdown (shutdown_type what) noexcept
	{
		return socket_.shutdown(what);
	}

	/// Return number of bytes readable without blocking
	/// \note macOS returns bytes plus source endpoint size
	[[nodiscard]] result<size_t> available () const noexcept
	{
		return socket_.available();
	}

	/// Return local endpoint to which this socket is bound
	[[nodiscard]] result<endpoint_type> local_endpoint () const noexcept
	{
		endpoint_type endpoint;
		auto size = endpoint.capacity();
		auto r = socket_.local_endpoint(endpoint.data(), &size);
		if (r)
		{
			endpoint.resize(size);
			return endpoint;
		}
		return unexpected{r.error()};
	}

	/// Return remote endpoint to which this socket is connected
	[[nodiscard]] result<endpoint_type> remote_endpoint () const noexcept
	{
		endpoint_type endpoint;
		auto size = endpoint.capacity();
		auto r = socket_.remote_endpoint(endpoint.data(), &size);
		if (r)
		{
			endpoint.resize(size);
			return endpoint;
		}
		return unexpected{r.error()};
	}

	/// Get socket \a option
	template <gettable_socket_option<protocol_type> GettableSocketOption>
	[[nodiscard]] result<void> get_option (GettableSocketOption &option) const noexcept
	{
		auto p = protocol();
		auto size = option.size(p);
		auto r = socket_.get_option(option.level(p), option.name(p), option.data(p), size);
		if (r)
		{
			return option.resize(p, size);
		}
		return r;
	}

	/// Set socket \a option
	template <settable_socket_option<protocol_type> SettableSocketOption>
	[[nodiscard]] result<void> set_option (const SettableSocketOption &option) noexcept
	{
		auto p = protocol();
		return socket_.set_option(option.level(p), option.name(p), option.data(p), option.size(p));
	}

protected:

	net::native_socket socket_;

	basic_socket (net::native_socket &&socket) noexcept
		: socket_{std::move(socket)}
	{
	}

	~basic_socket () = default;

	template <typename P>
	friend constexpr auto __socket::to_api () noexcept;
};

} // namespace pal::net
