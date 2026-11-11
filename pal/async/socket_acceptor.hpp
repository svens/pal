#pragma once

/**
 * \file pal/async/socket_acceptor.hpp
 * Asynchronous incoming connection acceptor
 */

#include <pal/async/handle.hpp>
#include <pal/net/basic_socket_acceptor.hpp>
#include <pal/net/socket_option.hpp>
#include <pal/require.hpp>
#include <pal/result.hpp>
#include <cstring>
#include <type_traits>
#include <utility>

namespace pal::async
{

namespace __socket_acceptor
{

template <typename AcceptableProtocol, typename Result>
struct op_accept
{
	using result_type = Result;
	using signature = void(task_ptr &&, result<result_type> &&) noexcept;

	template <typename F>
	static void dispatch (task &t, F &f) noexcept
	{
		const auto &op = __event_loop::io(t);
		task_ptr p{&t};

		const auto handle = net::__socket::from_sys(
			static_cast<std::underlying_type_t<net::__socket::handle_type>>(op.accepted)
		);

		if (op.native_error)
		{
			if (op.accepted != __event_loop::invalid_accepted)
			{
				net::native_socket::close(handle);
			}
			f(std::move(p), unexpected{__event_loop::error(op)});
			return;
		}

		typename AcceptableProtocol::endpoint endpoint;
		std::memcpy(endpoint.data(), op.endpoint.data(), sizeof(endpoint));
		const auto protocol = endpoint.protocol();
		f(
			std::move(p),
			result_type
			{
				.socket = net::__socket::to_api<typename AcceptableProtocol::socket>()(
					net::adopt(handle, protocol.family(), protocol.type())
				),
				.remote_endpoint = endpoint,
			}
		);
	}
};

} // namespace __socket_acceptor

/// Asynchronous socket acceptor: completing on the owning loop's thread, plus a curated subset of the synchronous
/// acceptor's own API. The acceptor arrives at \ref event_loop::make_handle bound and listening via the pal/net API;
/// make_handle switches it to non-blocking and registers it with the loop's backend.
///
/// Contracts: \c start_* and destruction are loop-thread-only (violations are undefined behavior, not checked).
/// Destruction cancels every op of this acceptor not yet dispatched as \c operation_canceled completions, delivered
/// from a subsequent run(); \ref event_loop::settle dispatches them before the loop is destroyed. A connection
/// accepted by a cancelled op is closed. A completion other than \c operation_canceled proves the acceptor was alive
/// at dispatch, so handlers start ops on it without a liveness check.
template <typename AcceptableProtocol>
class handle<net::basic_socket_acceptor<AcceptableProtocol>>
{
public:

	using protocol_type = AcceptableProtocol;
	using endpoint_type = AcceptableProtocol::endpoint;
	using socket_type = AcceptableProtocol::socket;

	/// Minimum payload window size of a task passed to \ref start_accept
	static constexpr size_t accept_window_size = __event_loop::accept_window_size;

	handle (handle &&) noexcept = default;
	handle &operator= (handle &&) noexcept = default;

	~handle () noexcept
	{
		state_.reset();
	}

	/// Completion payload of \ref start_accept
	struct accept_result
	{
		/// Accepted connection as a synchronous socket, ready for configuration and
		/// \ref event_loop::make_handle. Its blocking mode is unspecified.
		socket_type socket;

		/// Accepted connection's remote endpoint
		endpoint_type remote_endpoint;
	};

	/// Queue an asynchronous accept of the next incoming connection. \a t's payload window (\ref task::span) must
	/// hold at least \ref accept_window_size bytes; the operation uses it as scratch, leaving its contents
	/// unspecified. Pending accepts complete in start order. A per-connection failure (e.g. descriptor
	/// exhaustion) completes only the op it hit; re-arming immediately may hit it again, so back off (e.g.
	/// \ref event_loop::post_after). \a handler runs on the owning loop's thread, from a subsequent run().
	template <typename H>
	void start_accept (task_ptr &&t, H handler) noexcept
		requires __async::handler<H, void(task_ptr &&, result<accept_result> &&) noexcept>
	{
		pal_require(t->span().size() >= accept_window_size, "start_accept() window below accept_window_size");
		t->bind<__socket_acceptor::op_accept<AcceptableProtocol, accept_result>>(std::move(handler));
		__event_loop::start_accept(*state_, std::move(t));
	}

	/// \copydoc net::basic_socket_acceptor::local_endpoint
	[[nodiscard]] result<endpoint_type> local_endpoint () const noexcept
	{
		return acceptor_.local_endpoint();
	}

	/// \copydoc net::basic_socket_acceptor::native_socket
	[[nodiscard]] const net::native_socket &native_socket () const noexcept
	{
		return acceptor_.native_socket();
	}

	/// \copydoc net::basic_socket_acceptor::get_option
	template <net::gettable_socket_option<protocol_type> GettableSocketOption>
	[[nodiscard]] result<void> get_option (GettableSocketOption &option) const noexcept
	{
		return acceptor_.get_option(option);
	}

	/// \copydoc net::basic_socket_acceptor::set_option
	template <net::settable_socket_option<protocol_type> SettableSocketOption>
	[[nodiscard]] result<void> set_option (const SettableSocketOption &option) noexcept
	{
		return acceptor_.set_option(option);
	}

private:

	static_assert(std::is_trivially_copyable_v<endpoint_type>);
	static_assert(sizeof(endpoint_type) <= __event_loop::io_state::endpoint_capacity);

	__event_loop::socket_state_ptr state_;
	net::basic_socket_acceptor<AcceptableProtocol> acceptor_;

	handle (net::basic_socket_acceptor<AcceptableProtocol> &&acceptor, __event_loop::socket_state_ptr &&state) noexcept
		: state_{std::move(state)}
		, acceptor_{std::move(acceptor)}
	{
	}

	static result<handle>
	make (net::basic_socket_acceptor<AcceptableProtocol> &&acceptor, __event_loop::impl_type &loop) noexcept
	{
		if (auto r = acceptor.set_option(net::non_blocking_io{true}); !r)
		{
			return unexpected{r.error()};
		}

		auto state = __event_loop::make_socket_state(
			loop,
			static_cast<std::intptr_t>(net::__socket::to_sys(acceptor.native_socket().handle())),
			acceptor.native_socket().family(),
			__event_loop::socket_kind::acceptor
		);
		if (!state)
		{
			return unexpected{state.error()};
		}

		return handle{std::move(acceptor), std::move(*state)};
	}

	friend class event_loop;
};

} // namespace pal::async
