#pragma once

/**
 * \file pal/net/basic_socket_acceptor.hpp
 * Incoming connection acceptor
 */

#include <pal/net/concepts.hpp>
#include <pal/net/socket_base.hpp>

namespace pal::net
{

// clang-format off

/// Listens for and accepts incoming stream connections.
template <typename AcceptableProtocol>
class basic_socket_acceptor: public socket_base
{
public:

	/// Accepted socket protocol type
	using protocol_type = AcceptableProtocol;

	/// Protocol-specific endpoint type
	using endpoint_type = AcceptableProtocol::endpoint;

	/// Protocol-specific socket type returned by accept()
	using socket_type = AcceptableProtocol::socket;

	basic_socket_acceptor (basic_socket_acceptor &&) = default;
	basic_socket_acceptor &operator= (basic_socket_acceptor &&) = default;
	~basic_socket_acceptor () = default;

	/// Return true if acceptor holds a valid OS handle
	explicit operator bool () const noexcept
	{
		return socket_.operator bool();
	}

	/// Return acceptor protocol
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

	/// Bind acceptor to local \a endpoint
	[[nodiscard]] result<void> bind (const endpoint_type &endpoint) noexcept
	{
		return socket_.bind(endpoint.data(), endpoint.size());
	}

	/// Mark acceptor as ready to accept connections.
	/// \a backlog sets the maximum length of the pending-connection queue.
	[[nodiscard]] result<void> listen (int backlog = max_listen_connections) noexcept
	{
		return socket_.listen(backlog);
	}

	/// Accept the next pending connection. Blocks if none is queued.
	[[nodiscard]] result<socket_type> accept () noexcept
	{
		return socket_.accept(nullptr, nullptr).transform(__socket::to_api<socket_type>());
	}

	/// Accept the next pending connection, storing its remote endpoint in \a endpoint.
	[[nodiscard]] result<socket_type> accept (endpoint_type &endpoint) noexcept
	{
		auto size = endpoint.capacity();
		return socket_.accept(endpoint.data(), &size).and_then([&] (auto socket) -> result<socket_type>
		{
			return endpoint.resize(size).transform([&socket]
			{
				return __socket::to_api<socket_type>()(std::move(socket));
			});
		});
	}

	/// Return local endpoint to which this acceptor is bound
	[[nodiscard]] result<endpoint_type> local_endpoint () const noexcept
	{
		endpoint_type endpoint;
		auto size = endpoint.capacity();
		return socket_.local_endpoint(endpoint.data(), &size)
			.and_then([&] { return endpoint.resize(size); })
			.transform([&] { return endpoint; });
	}

	/// Get socket \a option
	template <gettable_socket_option<protocol_type> GettableSocketOption>
	[[nodiscard]] result<void> get_option (GettableSocketOption &option) const noexcept
	{
		auto p = protocol();
		auto size = option.size(p);
		return socket_.get_option(option.level(p), option.name(p), option.data(p), size)
			.and_then([&] { return option.resize(p, size); });
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

	basic_socket_acceptor (net::native_socket &&socket) noexcept
		: socket_{std::move(socket)}
	{
	}

	template <typename P>
	friend constexpr auto __socket::to_api () noexcept;
};

/// Open a socket acceptor for \a protocol.
template <acceptable_protocol AcceptableProtocol>
[[nodiscard]] result<basic_socket_acceptor<AcceptableProtocol>> make_socket_acceptor (
	const AcceptableProtocol &protocol) noexcept
{
	return open(protocol.family(), protocol.type(), protocol.protocol()).transform(
		__socket::to_api<basic_socket_acceptor<AcceptableProtocol>>()
	);
}

/// Open a socket acceptor for \a protocol, bind to \a endpoint, and listen.
template <acceptable_protocol AcceptableProtocol>
[[nodiscard]] result<basic_socket_acceptor<AcceptableProtocol>> make_socket_acceptor (
	const AcceptableProtocol &protocol,
	const typename AcceptableProtocol::endpoint &endpoint) noexcept
{
	return make_socket_acceptor(protocol).and_then(
		[&] (auto acceptor) -> result<basic_socket_acceptor<AcceptableProtocol>>
		{
			return acceptor.bind(endpoint)
				.and_then([&] { return acceptor.listen(); })
				.transform([&] { return std::move(acceptor); });
		}
	);
}

/// Adopt an existing OS \a handle as a socket acceptor for \a protocol.
template <acceptable_protocol AcceptableProtocol>
[[nodiscard]] result<basic_socket_acceptor<AcceptableProtocol>> make_socket_acceptor (
	const AcceptableProtocol &protocol,
	socket_base::native_handle_type handle) noexcept
{
	return __socket::to_api<basic_socket_acceptor<AcceptableProtocol>>()(native_socket{handle, protocol.family()});
}

// clang-format on

} // namespace pal::net
