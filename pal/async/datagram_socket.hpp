#pragma once

/**
 * \file pal/async/datagram_socket.hpp
 * Asynchronous datagram socket I/O
 */

#include <pal/async/handle.hpp>
#include <pal/net/basic_datagram_socket.hpp>
#include <pal/net/socket_option.hpp>
#include <pal/result.hpp>
#include <cstdint>
#include <cstring>
#include <memory>
#include <system_error>
#include <utility>

namespace pal::async
{

namespace __datagram_socket
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
			f(std::move(p), unexpected{std::error_code{op.native_error, std::system_category()}});
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
			f(std::move(p), unexpected{std::error_code{op.native_error, std::system_category()}});
			return;
		}
		t.span(t.span().first(op.bytes));
		f(std::move(p), result_type{.truncated = op.truncated});
	}
};

template <typename Protocol, typename Result>
struct op_receive_from
{
	using result_type = Result;
	using signature = void(task_ptr &&, result<result_type> &&) noexcept;

	template <typename F>
	static void dispatch (task &t, F &f) noexcept
	{
		auto &op = __event_loop::io(t);
		task_ptr p{&t};
		if (op.native_error)
		{
			f(std::move(p), unexpected{std::error_code{op.native_error, std::system_category()}});
			return;
		}
		t.span(t.span().first(op.bytes));
		f(std::move(p), result_type{.endpoint = source(op), .truncated = op.truncated});
	}

	static const Protocol::endpoint &source (__event_loop::io_state &op) noexcept
	{
		#ifdef __cpp_lib_start_lifetime_as
		{
			return *std::start_lifetime_as<typename Protocol::endpoint>(op.endpoint.data());
		}
		#else
		{
			return *reinterpret_cast<const typename Protocol::endpoint *>(op.endpoint.data());
		}
		#endif
	}
};

} // namespace __datagram_socket

/// Asynchronous datagram socket: completing on the owning loop's thread, plus a curated subset of the synchronous
/// socket's own API. The socket arrives at \ref event_loop::make_handle already opened (and typically bound) via the
/// pal/net API; make_handle switches it to non-blocking and registers it with the loop's backend.
///
/// Contracts: \c start_* and destruction are loop-thread-only (violations are undefined behavior, not checked).
/// Destruction cancels every op of this socket not yet dispatched as \c operation_canceled completions, delivered from
/// a subsequent run(); \ref event_loop::settle dispatches them before the loop is destroyed. A completion other than \c
/// operation_canceled proves the socket was alive at dispatch, so handlers start ops on it without a liveness check.
template <typename Protocol>
class handle<net::basic_datagram_socket<Protocol>>
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

	/// Completion payload of \ref start_receive_from
	struct receive_from_result
	{
		/// Datagram source, referencing storage inside the task: valid until the task's next
		/// \c start_*, copy to retain
		const endpoint_type &endpoint;

		/// True if the datagram was larger than the payload window and cut to it
		bool truncated;
	};

	/// Queue an asynchronous receive_from into \a t's payload window (\ref task::span). On completion the
	/// window is adjusted to the bytes retained; a datagram
	/// larger than the window is delivered with \c truncated set. The operation writes only within the
	/// window, never beyond it. \a handler runs on the owning loop's thread, from a subsequent run().
	template <typename H>
	void start_receive_from (task_ptr &&t, H handler) noexcept
		requires __async::handler<H, void(task_ptr &&, result<receive_from_result> &&) noexcept>
	{
		__event_loop::io(*t).endpoint_size = __event_loop::io_state::endpoint_capacity;
		t->bind<__datagram_socket::op_receive_from<Protocol, receive_from_result>>(std::move(handler));
		__event_loop::start_receive(*state_, std::move(t));
	}

	/// Completion payload of \ref start_send_to
	struct send_to_result
	{
	};

	/// Queue an asynchronous send_to of \a t's payload window (\ref task::span) to \a recipient. The window
	/// itself stays untouched: a datagram send is all-or-nothing. \a handler runs on the owning loop's
	/// thread, from a subsequent run().
	template <typename H>
	void start_send_to (task_ptr &&t, const endpoint_type &recipient, H handler) noexcept
		requires __async::handler<H, void(task_ptr &&, result<send_to_result> &&) noexcept>
	{
		auto &op = __event_loop::io(*t);
		std::memcpy(op.endpoint.data(), recipient.data(), sizeof(endpoint_type));
		op.endpoint_size = static_cast<uint32_t>(recipient.size());
		t->bind<__datagram_socket::op_send<send_to_result>>(std::move(handler));
		__event_loop::start_send(*state_, std::move(t));
	}

	/// Queue an asynchronous send_to of \a t's payload window (\ref task::span) back to the source of \a t's
	/// most recent completed receive, still held in the task's op state: \a t must arrive here from a
	/// \c start_receive_from completion, with no other operation on it in between. Otherwise behaves like
	/// \ref start_send_to.
	template <typename H>
	void start_reply (task_ptr &&t, H handler) noexcept
		requires __async::handler<H, void(task_ptr &&, result<send_to_result> &&) noexcept>
	{
		t->bind<__datagram_socket::op_send<send_to_result>>(std::move(handler));
		__event_loop::start_send(*state_, std::move(t));
	}

	/// Completion payload of \ref start_receive
	struct receive_result
	{
		/// True if the datagram was larger than the payload window and cut to it
		bool truncated;
	};

	/// Queue an asynchronous receive into \a t's payload window (\ref task::span) without reporting the
	/// datagram's source: for a socket connected through pal/net before \ref event_loop::make_handle, whose
	/// kernel delivers only the connected peer's datagrams. An ICMP error from that peer completes the next
	/// receive with the matching error. Otherwise behaves like \ref start_receive_from.
	template <typename H>
	void start_receive (task_ptr &&t, H handler) noexcept
		requires __async::handler<H, void(task_ptr &&, result<receive_result> &&) noexcept>
	{
		__event_loop::io(*t).endpoint_size = 0;
		t->bind<__datagram_socket::op_receive<receive_result>>(std::move(handler));
		__event_loop::start_receive(*state_, std::move(t));
	}

	/// Completion payload of \ref start_send
	struct send_result
	{
	};

	/// Queue an asynchronous send of \a t's payload window (\ref task::span) to the peer the socket was
	/// connected to through pal/net before \ref event_loop::make_handle. On an unconnected socket the
	/// operation completes with the platform's error. Otherwise behaves like \ref start_send_to.
	template <typename H>
	void start_send (task_ptr &&t, H handler) noexcept
		requires __async::handler<H, void(task_ptr &&, result<send_result> &&) noexcept>
	{
		__event_loop::io(*t).endpoint_size = 0;
		t->bind<__datagram_socket::op_send<send_result>>(std::move(handler));
		__event_loop::start_send(*state_, std::move(t));
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

	static_assert(std::is_trivially_copyable_v<endpoint_type>);
	static_assert(sizeof(endpoint_type) <= __event_loop::io_state::endpoint_capacity);

	__event_loop::socket_state_ptr state_;
	net::basic_datagram_socket<Protocol> socket_;

	handle (net::basic_datagram_socket<Protocol> &&socket, __event_loop::socket_state_ptr &&state) noexcept
		: state_{std::move(state)}
		, socket_{std::move(socket)}
	{
	}

	static result<handle>
	make (net::basic_datagram_socket<Protocol> &&socket, __event_loop::impl_type &loop) noexcept
	{
		if (auto r = socket.set_option(net::non_blocking_io{true}); !r)
		{
			return unexpected{r.error()};
		}

		auto state = __event_loop::make_socket_state(
			loop,
			static_cast<std::intptr_t>(net::__socket::to_sys(socket.native_socket().handle()))
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
