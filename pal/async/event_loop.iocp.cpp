#include <pal/version.hpp>

#if __pal_os_windows

#include <pal/async/__event_loop.hpp>
#include <pal/error.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <new>
#include <span>
#include <tuple>
#include <utility>
#include <winsock2.h>
#include <windows.h>

namespace pal::async::__event_loop
{

namespace
{

// Every other completion key is the socket_state the packet belongs to
constexpr ::ULONG_PTR wake_key = 1;

struct native_op: ::OVERLAPPED
{
	::WSABUF buffer;
	::DWORD bytes;
	::DWORD flags;
	::INT endpoint_size;
	bool send;
	task *self;
};

static_assert(sizeof(native_op) <= io_state::native_capacity);
static_assert(alignof(native_op) <= alignof(std::max_align_t));

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
	if (!n.send)
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
	if (!n.send && error == WSAEMSGSIZE)
	{
		on_truncated(op, n);
		return;
	}

	op.native_error = error;
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

	if (entry.lpOverlapped->Internal == 0)
	{
		on_success(op, n, entry.dwNumberOfBytesTransferred);
	}
	else
	{
		on_failure(op, n, state);
	}

	loop.ready_.push(t);
	return 0;
}

void harvest_batch (impl_type &loop, ::DWORD timeout) noexcept
{
	::ULONG count = 0;
	std::ignore = ::GetQueuedCompletionStatusEx(
		loop.port,
		loop.events.data(),
		static_cast<::ULONG>(loop.config_.poll_batch),
		&count,
		timeout,
		FALSE
	);

	size_t posts = 0;
	for (const auto &event: std::span{loop.events}.first(count))
	{
		posts += harvest(loop, event);
	}
	if (posts > 0)
	{
		loop.posts_in_flight_.fetch_sub(posts, std::memory_order_relaxed);
	}
}

void arm_socket_op (impl_type &loop, task &t, socket_state &state, socket_state::direction &d) noexcept
{
	auto &op = io(t);
	const auto payload = t.span();
	const bool send = &d == &state.send;

	auto &n = *new (op.native.data()) native_op{};
	n.buffer.buf = reinterpret_cast<::CHAR *>(payload.data());
	n.buffer.len = static_cast<::ULONG>(payload.size());
	n.send = send;
	n.self = &t;

	int r = 0;
	if (send)
	{
		r = ::WSASendTo(
			to_socket(state),
			&n.buffer,
			1,
			&n.bytes,
			0,
			reinterpret_cast<const ::sockaddr *>(op.endpoint.data()),
			static_cast<::INT>(op.endpoint_size),
			&n,
			nullptr
		);
	}
	else
	{
		n.endpoint_size = static_cast<::INT>(io_state::endpoint_capacity);
		r = ::WSARecvFrom(
			to_socket(state),
			&n.buffer,
			1,
			&n.bytes,
			&n.flags,
			reinterpret_cast<::sockaddr *>(op.endpoint.data()),
			&n.endpoint_size,
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

	if (!send && error == WSAEMSGSIZE)
	{
		on_truncated(op, n);
	}
	else
	{
		op.native_error = error;
	}
	loop.ready_.push(t);
}

} // namespace

void impl_type::poll (clock::duration timeout) noexcept
{
	harvest_batch(*this, to_milliseconds(timeout));
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

void impl_type::drain (socket_state &s, socket_state::direction &d) noexcept
{
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
		harvest_batch(*this, INFINITE);
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
