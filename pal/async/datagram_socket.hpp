#pragma once

/**
 * \file pal/async/datagram_socket.hpp
 * Asynchronous datagram socket I/O
 */

#include <pal/async/handle.hpp>
#include <pal/net/basic_datagram_socket.hpp>
#include <pal/net/socket_option.hpp>
#include <pal/require.hpp>
#include <pal/result.hpp>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <new>
#include <utility>

namespace pal::async
{

/// Completion payload of an asynchronous datagram receive (see \c start_receive_from in
/// \ref handle<net::basic_datagram_socket<Protocol>>).
template <typename Protocol>
struct receive_from_result
{
	/// Datagram source
	Protocol::endpoint endpoint;

	/// True if the datagram was larger than the payload window and cut to it
	bool truncated;
};

/// Completion payload of an asynchronous datagram send (see \c start_send_to in
/// \ref handle<net::basic_datagram_socket<Protocol>>).
template <typename Protocol>
struct send_to_result
{
};

namespace __datagram_socket
{

template <typename Protocol>
struct op_receive_from
{
	using result_type = receive_from_result<Protocol>;
	using signature = void(task_ptr &&, result<result_type> &&) noexcept;

	template <typename F>
	static void dispatch (task &t, F &f) noexcept
	{
		auto &op = __event_loop::io(t);
		task_ptr p{&t};
		if (op.ec)
		{
			f(std::move(p), unexpected{op.ec});
			return;
		}
		result_type r{.endpoint = {}, .truncated = op.truncated};
		std::memcpy(
			r.endpoint.data(), op.endpoint.data(), std::min<size_t>(op.endpoint_size, r.endpoint.capacity())
		);
		t.span(t.span().first(op.bytes));
		f(std::move(p), std::move(r));
	}
};

template <typename Protocol>
struct op_send_to
{
	using result_type = send_to_result<Protocol>;
	using signature = void(task_ptr &&, result<result_type> &&) noexcept;

	template <typename F>
	static void dispatch (task &t, F &f) noexcept
	{
		auto &op = __event_loop::io(t);
		task_ptr p{&t};
		if (op.ec)
		{
			f(std::move(p), unexpected{op.ec});
		}
		else
		{
			f(std::move(p), result_type{});
		}
	}
};

} // namespace __datagram_socket

/// Asynchronous datagram socket: completing on the owning loop's thread, plus a curated subset of the synchronous
/// socket's own API. The socket arrives at \ref event_loop::make_handle already opened (and typically bound) via the
/// pal/net API; make_handle switches it to non-blocking and registers it with the loop's poller.
///
/// Contracts: \c start_* and destruction are loop-thread-only (violations are undefined behavior, not checked).
/// Destruction with ops in flight synthesizes \c operation_canceled completions, delivered from a subsequent run() --
/// run the loop until they have been dispatched before destroying it.
template <typename Protocol>
class handle<net::basic_datagram_socket<Protocol>>
{
public:

	using protocol_type = Protocol;
	using endpoint_type = Protocol::endpoint;

	handle (handle &&) noexcept = default;
	handle &operator= (handle &&) noexcept = default;
	~handle () noexcept = default;

	/// Completion payload of \ref start_receive_from
	using receive_from_result = pal::async::receive_from_result<Protocol>;

	/// Queue an asynchronous receive_from into \a t's payload window (\ref task::span, which must be
	/// attached and non-empty). On completion the window is adjusted to the bytes retained; a datagram
	/// larger than the window is delivered with \c truncated set. The operation writes only within the
	/// window, never beyond it. \a handler runs on the owning loop's thread, from a subsequent run().
	template <typename H>
	void start_receive_from (task_ptr &&t, H handler) noexcept
		requires __async::handler<H, void(task_ptr &&, result<receive_from_result> &&) noexcept>
	{
		pal_require(!t->span().empty(), "start_receive_from without task payload storage");
		__event_loop::io(*t) = {};
		t->bind<__datagram_socket::op_receive_from<Protocol>>(std::move(handler));
		__event_loop::start_socket_op(std::move(t), *state_, state_->receive);
	}

	/// Completion payload of \ref start_send_to
	using send_to_result = pal::async::send_to_result<Protocol>;

	/// Queue an asynchronous send_to of \a t's payload window (\ref task::span) to \a recipient. The window
	/// itself stays untouched: a datagram send is all-or-nothing. \a handler runs on the owning loop's
	/// thread, from a subsequent run().
	template <typename H>
	void start_send_to (task_ptr &&t, const endpoint_type &recipient, H handler) noexcept
		requires __async::handler<H, void(task_ptr &&, result<send_to_result> &&) noexcept>
	{
		auto &op = __event_loop::io(*t);
		op = {};
		std::memcpy(op.endpoint.data(), recipient.data(), recipient.size());
		op.endpoint_size = static_cast<uint32_t>(recipient.size());
		t->bind<__datagram_socket::op_send_to<Protocol>>(std::move(handler));
		__event_loop::start_socket_op(std::move(t), *state_, state_->send);
	}

	/// \copydoc net::basic_socket::local_endpoint
	[[nodiscard]] result<endpoint_type> local_endpoint () const noexcept
	{
		return socket_.local_endpoint();
	}

	/// \copydoc net::basic_socket::native_socket
	[[nodiscard]] const net::native_socket &native_socket () const noexcept
	{
		return socket_.native_socket();
	}

	/// \copydoc net::basic_socket::get_option
	template <typename GettableSocketOption>
	[[nodiscard]] result<void> get_option (GettableSocketOption &option) const noexcept
	{
		return socket_.get_option(option);
	}

	/// \copydoc net::basic_socket::set_option
	template <typename SettableSocketOption>
	[[nodiscard]] result<void> set_option (const SettableSocketOption &option) noexcept
	{
		return socket_.set_option(option);
	}

private:

	static_assert(std::is_trivially_copyable_v<endpoint_type>);
	static_assert(sizeof(endpoint_type) <= __event_loop::io_state::endpoint_capacity);

	net::basic_datagram_socket<Protocol> socket_;
	__event_loop::socket_state_ptr state_;

	handle (net::basic_datagram_socket<Protocol> &&socket, __event_loop::socket_state_ptr &&state) noexcept
		: socket_{std::move(socket)}
		, state_{std::move(state)}
	{
	}

	static result<handle>
	make (net::basic_datagram_socket<Protocol> &&socket, __event_loop::impl_type &loop) noexcept
	{
		if (loop.register_socket_fn == nullptr)
		{
			return make_unexpected(std::errc::operation_not_supported);
		}

		if (auto r = socket.set_option(net::non_blocking_io{true}); !r)
		{
			return unexpected{r.error()};
		}

		__event_loop::socket_state_ptr state{new (std::nothrow) __event_loop::socket_state{
			.loop = &loop,
			.handle = static_cast<std::intptr_t>(net::__socket::to_sys(socket.native_socket().handle())),
		}};
		if (state == nullptr)
		{
			return make_unexpected(std::errc::not_enough_memory);
		}

		if (auto error = loop.register_socket_fn(loop, *state))
		{
			return unexpected{error};
		}

		return handle{std::move(socket), std::move(state)};
	}

	friend class event_loop;
};

} // namespace pal::async
