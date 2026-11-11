#include <pal/version.hpp>

#if __pal_os_windows

#include <pal/async/__event_loop.hpp>
#include <pal/error.hpp>
#include <pal/net/__socket.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <span>
#include <system_error>
#include <tuple>
#include <utility>
#include <winsock2.h>
#include <mswsock.h>
#include <windows.h>

namespace pal::async::__event_loop
{

namespace
{

// Every other completion key is the socket_state the packet belongs to
constexpr ::ULONG_PTR wake_key = 1;

constexpr auto accept_address_length = static_cast<::DWORD>(accept_address_size);

enum class op_kind: uint8_t
{
	receive,
	send,
	connect,
	accept,
};

struct native_op: ::OVERLAPPED
{
	::WSABUF buffer;
	::DWORD bytes;
	::DWORD flags;
	::INT endpoint_size;
	op_kind kind;
	task *self;
};

static_assert(sizeof(native_op) <= io_state::native_capacity);
static_assert(alignof(native_op) <= task::scratch_alignment);

native_op &native (::OVERLAPPED *overlapped) noexcept
{
	return *static_cast<native_op *>(overlapped);
}

::SOCKET to_socket (const socket_state &state) noexcept
{
	return static_cast<::SOCKET>(state.handle);
}

::HANDLE to_handle (const socket_state &state) noexcept
{
	return reinterpret_cast<::HANDLE>(state.handle);
}

constexpr ::DWORD to_milliseconds (clock::duration d) noexcept
{
	if (d == (clock::duration::max)())
	{
		return INFINITE;
	}
	const auto wait = (d < clock::duration::zero()) ? clock::duration::zero() : d;
	const auto msecs = std::chrono::ceil<std::chrono::milliseconds>(wait).count();
	constexpr auto cap = static_cast<std::chrono::milliseconds::rep>(INFINITE) - 1;
	return static_cast<::DWORD>((msecs > cap) ? cap : msecs);
}

void on_success (io_state &op, native_op &n, size_t bytes) noexcept
{
	op.bytes = bytes;
	op.truncated = false;
	if (n.kind == op_kind::receive)
	{
		op.endpoint_size = static_cast<uint32_t>(n.endpoint_size);
	}
}

void on_truncated (io_state &op, native_op &n) noexcept
{
	op.bytes = n.buffer.len;
	op.truncated = true;
	op.endpoint_size = static_cast<uint32_t>(n.endpoint_size);
}

// handle<> closes the socket only after socket_state teardown, so the handle here is always live.
void on_failure (io_state &op, native_op &n, const socket_state &state) noexcept
{
	::DWORD bytes = 0, flags = 0;
	if (::WSAGetOverlappedResult(to_socket(state), &n, &bytes, FALSE, &flags))
	{
		on_success(op, n, bytes);
		return;
	}

	const auto error = ::WSAGetLastError();
	if (n.kind == op_kind::receive && error == WSAEMSGSIZE)
	{
		on_truncated(op, n);
		return;
	}

	op.native_error = error;
}

[[nodiscard]] int overlapped_error (native_op &n, const socket_state &state, bool completed) noexcept
{
	::DWORD bytes = 0, flags = 0;
	if (completed || ::WSAGetOverlappedResult(to_socket(state), &n, &bytes, FALSE, &flags))
	{
		return 0;
	}
	return ::WSAGetLastError();
}

void finish_connect (io_state &op, native_op &n, socket_state &state, bool completed) noexcept
{
	state.kind = socket_kind::stream;

	op.native_error = overlapped_error(n, state, completed);
	if (op.native_error != 0)
	{
		return;
	}

	if (::setsockopt(to_socket(state), SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, nullptr, 0) == SOCKET_ERROR)
	{
		op.native_error = ::WSAGetLastError();
	}
}

void finish_accept (io_state &op, native_op &n, const socket_state &state, bool completed) noexcept
{
	op.native_error = overlapped_error(n, state, completed);
	if (op.native_error != 0)
	{
		return;
	}

	const auto listener = to_socket(state);
	const auto accepted = static_cast<::SOCKET>(op.accepted);
	if (::setsockopt(
			accepted,
			SOL_SOCKET,
			SO_UPDATE_ACCEPT_CONTEXT,
			reinterpret_cast<const char *>(&listener),
			sizeof(listener)
		) == SOCKET_ERROR)
	{
		op.native_error = ::WSAGetLastError();
		return;
	}

	::sockaddr *local = nullptr, *remote = nullptr;
	::INT local_size = 0, remote_size = 0;
	net::__socket::GetAcceptExSockaddrs(
		n.buffer.buf,
		0,
		accept_address_length,
		accept_address_length,
		&local,
		&local_size,
		&remote,
		&remote_size
	);
	std::memcpy(op.endpoint.data(), remote, static_cast<size_t>(remote_size));
}

size_t harvest (impl_type &loop, const ::OVERLAPPED_ENTRY &entry) noexcept
{
	if (entry.lpCompletionKey == wake_key)
	{
		loop.posted_.push(*reinterpret_cast<task *>(entry.lpOverlapped));
		return 1;
	}

	auto &state = *reinterpret_cast<socket_state *>(entry.lpCompletionKey);
	auto &n = native(entry.lpOverlapped);
	auto &t = *n.self;
	auto &op = io(t);

	--state.armed;

	const bool completed = entry.lpOverlapped->Internal == 0;
	switch (n.kind)
	{
		case op_kind::receive:
		case op_kind::send:
			if (completed)
			{
				on_success(op, n, entry.dwNumberOfBytesTransferred);
			}
			else
			{
				on_failure(op, n, state);
			}
			break;
		case op_kind::connect:
			finish_connect(op, n, state, completed);
			break;
		case op_kind::accept:
			finish_accept(op, n, state, completed);
			break;
	}

	loop.ready_.push(t);
	return 0;
}

std::error_code harvest_batch (impl_type &loop, ::DWORD timeout) noexcept
{
	::ULONG count = 0;
	const auto ok = ::GetQueuedCompletionStatusEx(
		loop.port,
		loop.events.data(),
		static_cast<::ULONG>(loop.config_.poll_batch),
		&count,
		timeout,
		FALSE
	);
	if (!ok)
	{
		const auto error = ::GetLastError();
		if (error != WAIT_TIMEOUT)
		{
			return {static_cast<int>(error), std::system_category()};
		}
	}

	size_t posts = 0;
	for (const auto &event: std::span{loop.events}.first(count))
	{
		posts += harvest(loop, event);
	}
	if (posts > 0)
	{
		loop.posts_in_flight_.fetch_sub(posts, std::memory_order_relaxed);
	}
	return {};
}

void arm_socket_op (impl_type &loop, task &t, socket_state &state, socket_state::direction &d) noexcept
{
	auto &op = io(t);
	const auto payload = t.span();

	auto &n = *new (op.native.data()) native_op{};
	n.buffer.buf = reinterpret_cast<::CHAR *>(payload.data());
	n.buffer.len = static_cast<::ULONG>(payload.size());
	n.kind = (&d == &state.send) ? op_kind::send : op_kind::receive;
	n.self = &t;

	auto *endpoint = (op.endpoint_size != 0) ? reinterpret_cast<::sockaddr *>(op.endpoint.data()) : nullptr;
	n.endpoint_size = static_cast<::INT>(op.endpoint_size);

	int r = 0;
	if (n.kind == op_kind::send)
	{
		r = ::WSASendTo(
			to_socket(state),
			&n.buffer,
			1,
			&n.bytes,
			0,
			endpoint,
			n.endpoint_size,
			&n,
			nullptr
		);
	}
	else
	{
		r = ::WSARecvFrom(
			to_socket(state),
			&n.buffer,
			1,
			&n.bytes,
			&n.flags,
			endpoint,
			(endpoint != nullptr) ? &n.endpoint_size : nullptr,
			&n,
			nullptr
		);
	}

	if (r == 0)
	{
		// immediate completion
		on_success(op, n, n.bytes);
		loop.ready_.push(t);
		return;
	}

	const auto error = ::WSAGetLastError();
	if (error == WSA_IO_PENDING)
	{
		++state.armed;
		return;
	}

	if (n.kind == op_kind::receive && error == WSAEMSGSIZE)
	{
		on_truncated(op, n);
	}
	else
	{
		op.native_error = error;
	}
	loop.ready_.push(t);
}

[[nodiscard]] int bind_if_unbound (const socket_state &state, ::ADDRESS_FAMILY family, int size) noexcept
{
	::sockaddr_storage local{};
	int local_size = sizeof(local);
	if (::getsockname(to_socket(state), reinterpret_cast<::sockaddr *>(&local), &local_size) == 0)
	{
		return 0;
	}

	const auto error = ::WSAGetLastError();
	if (error != WSAEINVAL)
	{
		return error;
	}

	local = {};
	local.ss_family = family;
	if (::bind(to_socket(state), reinterpret_cast<const ::sockaddr *>(&local), size) == SOCKET_ERROR)
	{
		return ::WSAGetLastError();
	}
	return 0;
}

void arm_connect (impl_type &loop, task &t, socket_state &state) noexcept
{
	auto &op = io(t);
	auto &n = *new (op.native.data()) native_op{};
	n.kind = op_kind::connect;
	n.self = &t;

	const auto *endpoint = reinterpret_cast<const ::sockaddr *>(op.endpoint.data());
	const auto endpoint_size = static_cast<int>(op.endpoint_size);

	auto error = bind_if_unbound(state, endpoint->sa_family, endpoint_size);
	if (error == 0)
	{
		if (net::__socket::ConnectEx(to_socket(state), endpoint, endpoint_size, nullptr, 0, nullptr, &n))
		{
			finish_connect(op, n, state, true);
			loop.ready_.push(t);
			return;
		}

		error = ::WSAGetLastError();
		if (error == WSA_IO_PENDING)
		{
			++state.armed;
			return;
		}
	}

	state.kind = socket_kind::stream;
	op.native_error = error;
	loop.ready_.push(t);
}

void arm_accept (impl_type &loop, task &t, socket_state &state) noexcept
{
	auto &op = io(t);
	auto &n = *new (op.native.data()) native_op{};
	n.kind = op_kind::accept;
	n.self = &t;
	n.buffer.buf = reinterpret_cast<::CHAR *>(t.span().data());

	const auto accepted = ::socket(state.family, SOCK_STREAM, IPPROTO_TCP);
	if (accepted == INVALID_SOCKET)
	{
		op.native_error = ::WSAGetLastError();
		loop.ready_.push(t);
		return;
	}
	op.accepted = static_cast<std::intptr_t>(accepted);

	if (net::__socket::AcceptEx(
			to_socket(state),
			accepted,
			n.buffer.buf,
			0,
			accept_address_length,
			accept_address_length,
			&n.bytes,
			&n
		))
	{
		finish_accept(op, n, state, true);
		loop.ready_.push(t);
		return;
	}

	const auto error = ::WSAGetLastError();
	if (error == WSA_IO_PENDING)
	{
		++state.armed;
		return;
	}

	op.native_error = error;
	loop.ready_.push(t);
}

} // namespace

std::error_code impl_type::poll (clock::duration timeout) noexcept
{
	return harvest_batch(*this, to_milliseconds(timeout));
}

void impl_type::post (task &t) const noexcept
{
	while (::PostQueuedCompletionStatus(port, 0, wake_key, reinterpret_cast<::LPOVERLAPPED>(&t)) == 0)
	{
	}
}

std::error_code impl_type::register_socket (socket_state &s) const noexcept
{
	const auto key = reinterpret_cast<::ULONG_PTR>(&s);
	if (::CreateIoCompletionPort(to_handle(s), port, key, 0) == nullptr)
	{
		return pal::this_thread::last_system_error();
	}

	s.receive.ready = true;
	s.send.ready = true;
	return {};
}

void impl_type::connect (socket_state &s, task &t) noexcept
{
	s.kind = socket_kind::connecting;
	s.send.pending.push(t);
	if (!s.queued)
	{
		s.queued = true;
		actionable_.push(s);
	}
}

void impl_type::drain (socket_state &s, socket_state::direction &d) noexcept
{
	if (s.kind == socket_kind::connecting)
	{
		arm_connect(*this, *d.pending.pop(), s);
		return;
	}

	if (s.kind == socket_kind::acceptor)
	{
		while (auto *t = d.pending.try_pop())
		{
			arm_accept(*this, *t, s);
		}
		return;
	}

	while (auto *t = d.pending.try_pop())
	{
		arm_socket_op(*this, *t, s, d);
	}
}

// Drains synchronously so every op of a destroyed socket is queued before the destructor returns, which a
// readiness backend gets for free by unqueueing them.
void impl_type::quiesce (socket_state &s) noexcept
{
	if (s.armed == 0)
	{
		return;
	}

	std::ignore = ::CancelIoEx(to_handle(s), nullptr);

	while (s.armed > 0)
	{
		std::ignore = harvest_batch(*this, INFINITE);
	}
}

result<impl_ptr> make_backend (const event_loop_config &config) noexcept
{
	unique_port port{::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1)};
	if (!port)
	{
		return unexpected{pal::this_thread::last_system_error()};
	}

	impl_ptr self{new (std::nothrow) impl_type{}};
	if (self == nullptr)
	{
		return make_unexpected(std::errc::not_enough_memory);
	}

	self->port = std::move(port);
	self->config_ = config;

	return self;
}

} // namespace pal::async::__event_loop

#endif // __pal_os_windows
