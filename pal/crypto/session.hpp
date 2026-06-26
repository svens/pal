#pragma once

/**
 * \file pal/crypto/session.hpp
 * TLS session: record-layer I/O over an application-supplied transport
 */

#include <pal/buffer.hpp>
#include <pal/crypto/secure_channel.hpp>
#include <pal/result.hpp>
#include <cstddef>
#include <memory>
#include <span>
#include <string_view>

namespace pal::crypto
{

// clang-format off

/// Satisfied when \a T can send a \a ConstBuffer and receive into a \a MutableBuffer,
/// both noexcept, both returning the byte count, and declares its transport type.
template <typename T, typename ConstBuffer, typename MutableBuffer>
concept io_device = requires(T &t, ConstBuffer out, MutableBuffer in)
{
	requires const_buffer<ConstBuffer>;
	requires mutable_buffer<MutableBuffer>;
	{ t.send(out) } noexcept -> std::same_as<result<size_t>>;
	{ t.receive(in) } noexcept -> std::same_as<result<size_t>>;
	requires std::same_as<std::remove_cvref_t<decltype(T::transport)>, transport_type>;
};

namespace __session
{

/// Internal virtual base: bridges the type-erased transport to the .cpp implementation.
struct device
{
	virtual result<size_t> send (std::span<const std::byte>) noexcept = 0;
	virtual result<size_t> receive (std::span<std::byte>) noexcept = 0;
	virtual ~device () = default;
};

/// Stack-allocated adapter: satisfies base for any concrete io_device T.
template <io_device<std::span<const std::byte>, std::span<std::byte>> T>
struct adapter: device
{
	T &t;

	explicit adapter (T &t) noexcept
		: t{t}
	{
	}

	result<size_t> send (std::span<const std::byte> buf) noexcept override
	{
		return t.send(buf);
	}

	result<size_t> receive (std::span<std::byte> buf) noexcept override
	{
		return t.receive(buf);
	}
};

} // namespace __session

// clang-format on

/// Established TLS session providing buffered record-layer I/O over any \c transport.
///
/// Move-only; moved-from instances have \c operator bool() == false.
///
/// Obtain via \c run_handshake (from a \c handshake_channel) or \c from (from an
/// existing \c connected_channel).
class session
{
public:

	session () noexcept;
	session (session &&) noexcept;
	session &operator= (session &&) noexcept;
	~session () noexcept;

	session (const session &) = delete;
	session &operator= (const session &) = delete;

	/// True when this session is live (false on moved-from).
	explicit operator bool () const noexcept;

	/// Wrap an already-established \a channel in a session.
	static result<session> from (connected_channel &&, transport_type) noexcept;

	/// Run the TLS handshake to completion over \a transport, returning the live session.
	template <io_device<std::span<const std::byte>, std::span<std::byte>> T>
	static result<session> run_handshake (T &transport, handshake_channel &handshake) noexcept
	{
		__session::adapter a{transport};
		return run_handshake_impl(a, handshake, T::transport);
	}

	/// Encrypt \a plain and flush all ciphertext through \a transport.
	///
	/// All-or-nothing: returns \c plain's byte count on success.
	template <io_device<std::span<const std::byte>, std::span<std::byte>> T>
	result<size_t> send (T &transport, const_buffer auto const &plain) noexcept
	{
		__session::adapter a{transport};
		return send_impl(a, std::as_bytes(std::span{plain}));
	}

	/// Receive at least one plaintext byte from \a transport into \a buf.
	///
	/// Returns \c closed error when the peer sends \c close_notify.
	template <io_device<std::span<const std::byte>, std::span<std::byte>> T>
	result<size_t> receive (T &transport, mutable_buffer auto &&buf) noexcept
	{
		__session::adapter a{transport};
		return receive_impl(a, std::as_writable_bytes(std::span{buf}));
	}

	/// Send a TLS \c close_notify alert through \a transport.
	template <io_device<std::span<const std::byte>, std::span<std::byte>> T>
	result<void> close_notify (T &transport) noexcept
	{
		__session::adapter a{transport};
		return close_notify_impl(a);
	}

	/// Return the peer's verified certificate (null certificate if none was presented).
	[[nodiscard]] result<certificate> peer_certificate () const noexcept;

	/// Return the ALPN-selected protocol, or an empty view if none was negotiated.
	[[nodiscard]] std::string_view selected_protocol () const noexcept;

	/// Return the maximum plaintext size for a single send call.
	///
	/// For DTLS (datagram transport), this is the MTU-constrained limit.
	/// For TLS (stream transport), this is \c SIZE_MAX (TLS fragments automatically).
	[[nodiscard]] size_t max_message_size () const noexcept;

private:

	struct impl_type;
	using impl_ptr = std::unique_ptr<impl_type>;
	impl_ptr impl_;

	explicit session (impl_ptr) noexcept;

	static result<session> run_handshake_impl (__session::device &, handshake_channel &, transport_type) noexcept;
	result<size_t> send_impl (__session::device &, std::span<const std::byte>) noexcept;
	result<size_t> receive_impl (__session::device &, std::span<std::byte>) noexcept;
	result<void> close_notify_impl (__session::device &) noexcept;
};

} // namespace pal::crypto
