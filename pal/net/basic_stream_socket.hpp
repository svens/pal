#pragma once

/**
 * \file pal/net/basic_stream_socket.hpp
 * Generic stream-oriented socket
 */

#include <pal/buffer.hpp>
#include <pal/net/basic_socket.hpp>

namespace pal::net
{

// clang-format off

/// Stream socket for sequenced, reliable, bidirectional byte-stream communication.
///
/// \note Vectored I/O supports up to socket_base::io_vector_max_size buffers.
/// If a send/receive call supplies more, it returns std::errc::argument_list_too_long.
template <typename Protocol>
class basic_stream_socket: public basic_socket<Protocol>
{
public:

	using typename basic_socket<Protocol>::protocol_type;
	using typename basic_socket<Protocol>::endpoint_type;

	/// Receive into \a bufs.
	template <pal::mutable_buffer_sequence MutableBufferSequence>
	[[nodiscard]] result<size_t> receive (
		const MutableBufferSequence &bufs,
		socket_base::message_flags flags = {}) noexcept
	{
		if (__socket::message message{}; message.set(bufs))
		{
			message.flags(flags);
			return this->socket_.receive(message);
		}
		return make_unexpected(std::errc::argument_list_too_long);
	}

	/// Send \a bufs.
	template <pal::const_buffer_sequence ConstBufferSequence>
	[[nodiscard]] result<size_t> send (
		const ConstBufferSequence &bufs,
		socket_base::message_flags flags = {}) noexcept
	{
		if (__socket::message message{}; message.set(bufs))
		{
			message.flags(flags);
			return this->socket_.send(message);
		}
		return make_unexpected(std::errc::argument_list_too_long);
	}
};

/// Open a stream socket for \a protocol.
template <protocol Protocol>
[[nodiscard]] result<basic_stream_socket<Protocol>> make_stream_socket (
	const Protocol &protocol) noexcept
{
	return open(protocol.family(), protocol.type(), protocol.protocol()).transform(
		__socket::to_api<basic_stream_socket<Protocol>>()
	);
}

/// Open a stream socket for \a protocol and bind it to \a endpoint.
template <protocol Protocol>
[[nodiscard]] result<basic_stream_socket<Protocol>> make_stream_socket (
	const Protocol &protocol,
	const typename Protocol::endpoint &endpoint) noexcept
{
	return make_stream_socket(protocol).and_then(
		[&] (auto socket) -> result<basic_stream_socket<Protocol>>
		{ return socket.bind(endpoint).transform([&] { return std::move(socket); }); }
	);
}

/// Adopt an existing OS \a handle as a stream socket for \a protocol.
template <protocol Protocol>
[[nodiscard]] result<basic_stream_socket<Protocol>> make_stream_socket (
	const Protocol &protocol,
	socket_base::native_handle_type handle) noexcept
{
	return __socket::to_api<basic_stream_socket<Protocol>>()(native_socket{handle, protocol.family()});
}

// clang-format on

} // namespace pal::net
