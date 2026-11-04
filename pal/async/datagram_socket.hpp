#pragma once

/**
 * \file pal/async/datagram_socket.hpp
 * Asynchronous datagram socket I/O
 */

#include <pal/async/handle.hpp>
#include <pal/async/multishot.hpp>
#include <pal/net/basic_datagram_socket.hpp>
#include <pal/net/socket_option.hpp>
#include <pal/require.hpp>
#include <pal/result.hpp>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <system_error>
#include <utility>

namespace pal::async
{

/// Completion payload of an asynchronous datagram receive (see \c start_receive_from in
/// \ref handle<net::basic_datagram_socket<Protocol>>).
template <typename Protocol>
struct receive_from_result
{
	/// Datagram source, referencing storage inside the task: valid until the task's next
	/// \c start_*, copy to retain
	const Protocol::endpoint &endpoint;

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

template <typename Protocol>
struct op_send_to
{
	using result_type = send_to_result<Protocol>;
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
		t->bind<__datagram_socket::op_receive_from<Protocol>>(std::move(handler));
		state_->start_op(std::move(t), state_->receive, false);
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
		std::memcpy(op.endpoint.data(), recipient.data(), sizeof(endpoint_type));
		op.endpoint_size = static_cast<uint32_t>(recipient.size());
		t->bind<__datagram_socket::op_send_to<Protocol>>(std::move(handler));
		state_->start_op(std::move(t), state_->send, false);
	}

	/// Queue an asynchronous send_to of \a t's payload window (\ref task::span) back to the source of \a t's
	/// most recent completed receive, still held in the task's op state: \a t must arrive here from a
	/// \c start_receive_from completion, with no other operation on it in between. Otherwise behaves like
	/// \ref start_send_to.
	template <typename H>
	void start_reply (task_ptr &&t, H handler) noexcept
		requires __async::handler<H, void(task_ptr &&, result<send_to_result> &&) noexcept>
	{
		pal_require(__event_loop::io(*t).endpoint_size != 0, "start_reply without a completed receive");
		t->bind<__datagram_socket::op_send_to<Protocol>>(std::move(handler));
		state_->start_op(std::move(t), state_->send, false);
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

protected:

	/// \cond
	net::basic_datagram_socket<Protocol> socket_;
	__event_loop::socket_state_ptr state_;

	handle (net::basic_datagram_socket<Protocol> &&socket, __event_loop::socket_state_ptr &&state) noexcept
		: socket_{std::move(socket)}
		, state_{std::move(state)}
	{
	}

	static result<__event_loop::socket_state_ptr>
	make_state (net::basic_datagram_socket<Protocol> &socket, __event_loop::impl_type &loop) noexcept
	{
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

		if (auto error = loop.register_socket(*state))
		{
			return unexpected{error};
		}

		return state;
	}
	/// \endcond

private:

	static_assert(std::is_trivially_copyable_v<endpoint_type>);
	static_assert(sizeof(endpoint_type) <= __event_loop::io_state::endpoint_capacity);

	static result<handle>
	make (net::basic_datagram_socket<Protocol> &&socket, __event_loop::impl_type &loop) noexcept
	{
		return make_state(socket, loop).transform([&socket](__event_loop::socket_state_ptr &&state) noexcept
		{
			return handle{std::move(socket), std::move(state)};
		});
	}

	friend class event_loop;
};

/// Asynchronous datagram socket in multishot receive mode (see \ref multishot): receives from \ref
/// event_loop::make_handle until destruction into tasks from a pool this handle owns, and sends single-shot
/// like \ref handle<net::basic_datagram_socket<Protocol>>, which documents the shared members.
///
/// Contracts, beyond those of the single-shot handle: the receive handler sees no errors, per-datagram errors
/// are dropped. Destruction is silent for receives: completions not yet dispatched return to the pool and the
/// handler does not run for them. Tasks the application holds stay valid after destruction and may be
/// dropped on the loop thread at any time before the loop is destroyed.
template <typename Protocol>
class handle<multishot<net::basic_datagram_socket<Protocol>>>
	: private handle<net::basic_datagram_socket<Protocol>>
{
	using base = handle<net::basic_datagram_socket<Protocol>>;

public:

	using typename base::protocol_type;
	using typename base::endpoint_type;
	using typename base::receive_from_result;
	using typename base::send_to_result;

	handle (handle &&) noexcept = default;
	handle &operator= (handle &&) noexcept = default;
	~handle () noexcept = default;

	using base::start_send_to;
	using base::start_reply;
	using base::local_endpoint;
	using base::native_socket;
	using base::get_option;
	using base::set_option;

	/// Restore \a t's payload window to the full capacity of its pool slot (at least
	/// \ref multishot_config::buffer_size), for replies larger than the received datagram. \a t must come from
	/// this handle's pool.
	void reset_span (task &t) noexcept
	{
		auto *pool = this->state_->pool.get();
		pal_require(__task::attorney::recycler_of(t) == pool, "reset_span() on task from another pool");
		t.span(pool->buffer(t));
	}

private:

	using base::base;

	template <typename H>
	static result<handle>
	make (multishot<net::basic_datagram_socket<Protocol>, H> &&resource, __event_loop::impl_type &loop) noexcept
		requires __async::handler<H, void(task_ptr &&, result<receive_from_result> &&) noexcept>
	{
		auto state = base::make_state(resource.resource, loop);
		if (!state)
		{
			return unexpected{state.error()};
		}

		auto pool = __event_loop::make_multishot_pool(**state, resource.config);
		if (!pool)
		{
			return unexpected{pool.error()};
		}

		(*pool)->handler.template arm<__datagram_socket::op_receive_from<Protocol>>(std::move(resource.handler));
		(*state)->pool = std::move(*pool);

		return handle{std::move(resource.resource), std::move(*state)};
	}

	friend class event_loop;
};

} // namespace pal::async
