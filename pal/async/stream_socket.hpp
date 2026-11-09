#pragma once

/**
 * \file pal/async/stream_socket.hpp
 * Asynchronous stream socket I/O
 */

#include <pal/async/handle.hpp>
#include <pal/net/basic_stream_socket.hpp>
#include <pal/net/socket_option.hpp>
#include <pal/result.hpp>
#include <cstdint>
#include <utility>

namespace pal::async
{

namespace __stream_socket
{

template <typename Result>
struct op_send
{
	using result_type = Result;
	using signature = void(task_ptr &&, result<result_type> &&) noexcept;

	template <typename F>
	static void dispatch (task &t, F &f) noexcept
	{
		const auto &op = __event_loop::io(t);
		task_ptr p{&t};
		if (op.native_error)
		{
			f(std::move(p), unexpected{__event_loop::error(op)});
		}
		else
		{
			f(std::move(p), result_type{});
		}
	}
};

template <typename Result>
struct op_receive
{
	using result_type = Result;
	using signature = void(task_ptr &&, result<result_type> &&) noexcept;

	template <typename F>
	static void dispatch (task &t, F &f) noexcept
	{
		const auto &op = __event_loop::io(t);
		task_ptr p{&t};
		if (op.native_error)
		{
			f(std::move(p), unexpected{__event_loop::error(op)});
			return;
		}
		t.span(t.span().first(op.bytes));
		f(std::move(p), result_type{});
	}
};

} // namespace __stream_socket

/// Asynchronous stream socket: completing on the owning loop's thread, plus a curated subset of the synchronous
/// socket's own API. The socket arrives at \ref event_loop::make_handle already connected via the pal/net API;
/// make_handle switches it to non-blocking and registers it with the loop's backend.
///
/// Half-close has no member: shut down the send direction through \ref native_socket only after every
/// \ref start_send has completed, or queued data is cut off behind the FIN.
///
/// Contracts: \c start_* and destruction are loop-thread-only (violations are undefined behavior, not checked).
/// Destruction cancels every op of this socket not yet dispatched as \c operation_canceled completions, delivered from
/// a subsequent run(); \ref event_loop::settle dispatches them before the loop is destroyed. A completion other than \c
/// operation_canceled proves the socket was alive at dispatch, so handlers start ops on it without a liveness check.
template <typename Protocol>
class handle<net::basic_stream_socket<Protocol>>
{
public:

	using protocol_type = Protocol;
	using endpoint_type = Protocol::endpoint;

	handle (handle &&) noexcept = default;
	handle &operator= (handle &&) noexcept = default;

	~handle () noexcept
	{
		state_.reset();
	}

	/// Completion payload of \ref start_receive
	struct receive_result
	{
	};

	/// Queue an asynchronous receive into \a t's payload window (\ref task::span), which must be non-empty. On
	/// completion the window is adjusted to the bytes received, which may be fewer than the window holds; an empty
	/// window reports end of stream (the peer shut down its send direction). \a handler runs on the owning loop's
	/// thread, from a subsequent run().
	template <typename H>
	void start_receive (task_ptr &&t, H handler) noexcept
		requires __async::handler<H, void(task_ptr &&, result<receive_result> &&) noexcept>
	{
		__event_loop::io(*t).endpoint_size = 0;
		t->bind<__stream_socket::op_receive<receive_result>>(std::move(handler));
		__event_loop::start_receive(*state_, std::move(t));
	}

	/// Completion payload of \ref start_send
	struct send_result
	{
	};

	/// Queue an asynchronous send of \a t's whole payload window (\ref task::span). Completes once every byte
	/// of the window is handed to the kernel, or with an error, which leaves the count already sent unreported.
	/// Sends queued on one socket go out in start order without interleaving. The window itself stays
	/// untouched. \a handler runs on the owning loop's thread, from a subsequent run().
	template <typename H>
	void start_send (task_ptr &&t, H handler) noexcept
		requires __async::handler<H, void(task_ptr &&, result<send_result> &&) noexcept>
	{
		auto &op = __event_loop::io(*t);
		op.endpoint_size = 0;
		op.bytes = 0;
		t->bind<__stream_socket::op_send<send_result>>(std::move(handler));
		__event_loop::start_send(*state_, std::move(t));
	}

	/// \copydoc net::basic_socket::local_endpoint
	[[nodiscard]] result<endpoint_type> local_endpoint () const noexcept
	{
		return socket_.local_endpoint();
	}

	/// \copydoc net::basic_socket::remote_endpoint
	[[nodiscard]] result<endpoint_type> remote_endpoint () const noexcept
	{
		return socket_.remote_endpoint();
	}

	/// \copydoc net::basic_socket::native_socket
	[[nodiscard]] const net::native_socket &native_socket () const noexcept
	{
		return socket_.native_socket();
	}

	/// \copydoc net::basic_socket::get_option
	template <net::gettable_socket_option<protocol_type> GettableSocketOption>
	[[nodiscard]] result<void> get_option (GettableSocketOption &option) const noexcept
	{
		return socket_.get_option(option);
	}

	/// \copydoc net::basic_socket::set_option
	template <net::settable_socket_option<protocol_type> SettableSocketOption>
	[[nodiscard]] result<void> set_option (const SettableSocketOption &option) noexcept
	{
		return socket_.set_option(option);
	}

private:

	__event_loop::socket_state_ptr state_;
	net::basic_stream_socket<Protocol> socket_;

	handle (net::basic_stream_socket<Protocol> &&socket, __event_loop::socket_state_ptr &&state) noexcept
		: state_{std::move(state)}
		, socket_{std::move(socket)}
	{
	}

	static result<handle>
	make (net::basic_stream_socket<Protocol> &&socket, __event_loop::impl_type &loop) noexcept
	{
		if (auto r = socket.set_option(net::non_blocking_io{true}); !r)
		{
			return unexpected{r.error()};
		}

		auto state = __event_loop::make_socket_state(
			loop,
			static_cast<std::intptr_t>(net::__socket::to_sys(socket.native_socket().handle())),
			__event_loop::socket_kind::stream
		);
		if (!state)
		{
			return unexpected{state.error()};
		}

		return handle{std::move(socket), std::move(*state)};
	}

	friend class event_loop;
};

} // namespace pal::async
