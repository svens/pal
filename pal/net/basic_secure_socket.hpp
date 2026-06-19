#pragma once

/**
 * \file pal/net/basic_secure_socket.hpp
 * TLS/DTLS socket layered over a plain stream or connected datagram socket
 */

#include <pal/buffer.hpp>
#include <pal/crypto/session.hpp>
#include <pal/net/basic_datagram_socket.hpp>
#include <pal/net/basic_stream_socket.hpp>
#include <pal/result.hpp>
#include <cstddef>
#include <span>
#include <string_view>

namespace pal::net
{

namespace __socket //{{{1
{

template <typename>
inline constexpr crypto::transport transport_v = crypto::transport::stream;

template <typename Protocol>
inline constexpr crypto::transport transport_v<basic_datagram_socket<Protocol>> = crypto::transport::datagram;

} // namespace __socket

// basic_secure_socket {{{1

/// TLS/DTLS socket.
///
/// Pure TLS/DTLS layer over a caller-owned \c Protocol::socket. The socket must be
/// fully configured (options, connect) before transferring ownership via the
/// factory functions; no transport API is exposed after that point.
///
/// Move-only; moved-from instances have \c operator bool() == false.
template <typename Protocol, crypto::transport Transport>
class basic_secure_socket: public socket_base
{
public:

	using protocol_type = Protocol;
	using endpoint_type = Protocol::endpoint;
	using socket_type = Protocol::socket;

	basic_secure_socket (basic_secure_socket &&) noexcept = default;
	basic_secure_socket &operator= (basic_secure_socket &&) noexcept = default;

	basic_secure_socket (const basic_secure_socket &) = delete;
	basic_secure_socket &operator= (const basic_secure_socket &) = delete;

	~basic_secure_socket () = default;

	/// True when this socket holds a live TLS/DTLS session (false on moved-from).
	explicit operator bool () const noexcept
	{
		return static_cast<bool>(session_);
	}

	/// Encrypt \a buf and flush all ciphertext to the peer.
	[[nodiscard]] result<size_t> send (const_buffer auto const &buf) noexcept
	{
		return session_.send(transport_, buf);
	}

	/// Receive at least one byte of decrypted plaintext into \a buf.
	///
	/// Blocks until >=1 plaintext byte is available, peer sends \c close_notify
	/// (returns \c closed error), or a transport error occurs.
	[[nodiscard]] result<size_t> receive (mutable_buffer auto &&buf) noexcept
	{
		return session_.receive(transport_, buf);
	}

	/// Send a TLS/DTLS \c close_notify alert to the peer.
	[[nodiscard]] result<void> close_notify () noexcept
	{
		return session_.close_notify(transport_);
	}

	/// Return the peer's verified certificate (null certificate if none was presented).
	[[nodiscard]] result<crypto::certificate> peer_certificate () const noexcept
	{
		return session_.peer_certificate();
	}

	/// Return the ALPN-selected protocol, or an empty view if none was negotiated.
	[[nodiscard]] std::string_view selected_protocol () const noexcept
	{
		return session_.selected_protocol();
	}

	/// Release the underlying OS socket handle and destroy the TLS/DTLS session.
	///
	/// After this call, \c operator bool() returns false. No \c close_notify is sent.
	[[nodiscard]] net::native_socket release () noexcept
	{
		session_ = {};
		return transport_.socket.release();
	}

private:

	/// Adapts the owned socket to the \c pal::crypto::io_device concept.
	///
	/// For stream sockets: translates recv()==0 (TCP FIN) to \c secure_channel_errc::closed.
	/// For datagram sockets: passes results through unchanged (no FIN semantics).
	struct device
	{
		static constexpr crypto::transport transport_value = Transport;

		socket_type socket;

		explicit device (socket_type s) noexcept
			: socket{std::move(s)}
		{
		}

		result<size_t> send (std::span<const std::byte> buf) noexcept
		{
			return socket.send(buf);
		}

		result<size_t> receive (std::span<std::byte> buf) noexcept
		{
			auto received = socket.receive(buf);
			if constexpr (Transport == crypto::transport::stream)
			{
				if (received && *received == 0)
				{
					return unexpected{crypto::make_error_code(crypto::secure_channel_errc::closed)};
				}
			}
			return received;
		}
	};

	device transport_;
	crypto::session session_;

	basic_secure_socket (device transport, crypto::session session) noexcept
		: transport_{std::move(transport)}
		, session_{std::move(session)}
	{
	}

	template <typename Socket>
	friend result<basic_secure_socket<typename Socket::protocol_type, __socket::transport_v<Socket>>>
	make_secure_socket (
		Socket,
		const crypto::connector<__socket::transport_v<Socket>> &,
		const crypto::connector_handshake_options &
	) noexcept;

	template <typename Socket>
	friend result<basic_secure_socket<typename Socket::protocol_type, __socket::transport_v<Socket>>>
	make_secure_socket (
		Socket,
		const crypto::acceptor<__socket::transport_v<Socket>> &,
		const crypto::acceptor_handshake_options &
	) noexcept;
};

// make_secure_socket {{{1

/// Run a client-side TLS/DTLS handshake over a pre-connected \a socket.
template <typename Socket>
[[nodiscard]] result<basic_secure_socket<typename Socket::protocol_type, __socket::transport_v<Socket>>>
make_secure_socket (
	Socket socket,
	const crypto::connector<__socket::transport_v<Socket>> &connector,
	const crypto::connector_handshake_options &opts = {}) noexcept
{
	using Protocol = Socket::protocol_type;
	using secure_socket = basic_secure_socket<Protocol, __socket::transport_v<Socket>>;

	auto handshake = connector.connect(opts);
	if (!handshake)
	{
		return unexpected(handshake.error());
	}

	typename secure_socket::device transport{std::move(socket)};
	auto session = crypto::session::run_handshake(transport, *handshake);
	if (!session)
	{
		return unexpected(session.error());
	}

	return secure_socket{std::move(transport), std::move(*session)};
}

/// Run a server-side TLS/DTLS handshake over a pre-accepted \a socket.
template <typename Socket>
[[nodiscard]] result<basic_secure_socket<typename Socket::protocol_type, __socket::transport_v<Socket>>>
make_secure_socket (
	Socket socket,
	const crypto::acceptor<__socket::transport_v<Socket>> &acceptor,
	const crypto::acceptor_handshake_options &opts = {}) noexcept
{
	using Protocol = Socket::protocol_type;
	using secure_socket = basic_secure_socket<Protocol, __socket::transport_v<Socket>>;

	auto handshake = acceptor.accept(opts);
	if (!handshake)
	{
		return unexpected(handshake.error());
	}

	typename secure_socket::device transport{std::move(socket)};
	auto session = crypto::session::run_handshake(transport, *handshake);
	if (!session)
	{
		return unexpected(session.error());
	}

	return secure_socket{std::move(transport), std::move(*session)};
}

// }}}1

} // namespace pal::net
