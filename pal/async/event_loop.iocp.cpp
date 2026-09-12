#include <pal/version.hpp>

#if __pal_os_windows

#include <pal/async/__event_loop.hpp>
#include <pal/error.hpp>

#include <array>
#include <chrono>
#include <cstddef>
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

using poll_array = std::array<::OVERLAPPED_ENTRY, event_loop_config::max_poll_batch>;

using unique_port = unique_handle<::HANDLE, ::CloseHandle, nullptr>;

// Every other completion key is the socket_state the packet belongs to
constexpr ::ULONG_PTR wake_key = 1;

struct iocp_loop: impl_type
{
	unique_port port{};
	poll_array events{};

	void do_post (task &t) noexcept override
	{
		while (::PostQueuedCompletionStatus(port, 0, wake_key, reinterpret_cast<::LPOVERLAPPED>(&t)) == 0)
		{
		}
	}

	std::error_code register_socket (socket_state &state) noexcept override;
	void close_socket (socket_state &state) noexcept override;

	size_t harvest (const ::OVERLAPPED_ENTRY &entry) noexcept;
	size_t harvest_batch (::DWORD timeout) noexcept;
	void arm_socket_op (task &t, socket_state &state, socket_state::direction &d) noexcept;
};

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

constexpr ::DWORD to_milliseconds (impl_type::clock::duration d) noexcept
{
	const auto wait = (d < impl_type::clock::duration::zero()) ? impl_type::clock::duration::zero() : d;
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

size_t iocp_loop::harvest (const ::OVERLAPPED_ENTRY &entry) noexcept
{
	if (entry.lpCompletionKey == wake_key)
	{
		posted_.push(*reinterpret_cast<task *>(entry.lpOverlapped));
		return 1;
	}

	auto &state = *reinterpret_cast<socket_state *>(entry.lpCompletionKey);
	auto &n = native(entry.lpOverlapped);
	auto &t = *n.self;
	auto &op = io(t);

	--(n.send ? state.send : state.receive).armed;

	if (entry.lpOverlapped->Internal == 0)
	{
		on_success(op, n, entry.dwNumberOfBytesTransferred);
	}
	else
	{
		on_failure(op, n, state);
	}

	ready_.push(t);
	return 0;
}

size_t iocp_loop::harvest_batch (::DWORD timeout) noexcept
{
	::ULONG count = 0;
	std::ignore = ::GetQueuedCompletionStatusEx(
		port,
		events.data(),
		static_cast<::ULONG>(config_.poll_batch),
		&count,
		timeout,
		FALSE
	);

	size_t posts = 0;
	for (const auto &event: std::span{events}.first(count))
	{
		posts += harvest(event);
	}
	return posts;
}

void iocp_poll (impl_type &base, impl_type::clock::duration timeout) noexcept
{
	auto &self = static_cast<iocp_loop &>(base);

	::DWORD ms = INFINITE;
	if (timeout != (impl_type::clock::duration::max)())
	{
		ms = to_milliseconds(timeout);
	}

	if (const auto posts = self.harvest_batch(ms))
	{
		self.posts_in_flight_.fetch_sub(posts, std::memory_order_relaxed);
	}
}

std::error_code iocp_loop::register_socket (socket_state &state) noexcept
{
	const auto key = reinterpret_cast<::ULONG_PTR>(&state);
	if (::CreateIoCompletionPort(to_handle(state), port, key, 0) == nullptr)
	{
		return pal::this_thread::last_system_error();
	}

	state.receive.ready = true;
	state.send.ready = true;
	return {};
}

void iocp_loop::arm_socket_op (task &t, socket_state &state, socket_state::direction &d) noexcept
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
		ready_.push(t);
		return;
	}

	const auto error = ::WSAGetLastError();
	if (error == WSA_IO_PENDING)
	{
		++d.armed;
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
	ready_.push(t);
}

void iocp_drain_socket (impl_type &base, socket_state &state, socket_state::direction &d) noexcept
{
	auto &self = static_cast<iocp_loop &>(base);
	while (auto *t = d.pending.try_pop())
	{
		self.arm_socket_op(*t, state, d);
	}
}

// Drains synchronously so every op of a destroyed socket is queued before the destructor returns, which a
// readiness backend gets for free by unqueueing them.
void iocp_loop::close_socket (socket_state &state) noexcept
{
	if (state.armed() > 0)
	{
		std::ignore = ::CancelIoEx(to_handle(state), nullptr);

		size_t posts = 0;
		while (state.armed() > 0)
		{
			posts += harvest_batch(INFINITE);
		}
		if (posts > 0)
		{
			posts_in_flight_.fetch_sub(posts, std::memory_order_relaxed);
		}
	}

	impl_type::close_socket(state);
}

} // namespace

result<impl_ptr> make_iocp_loop (const event_loop_config &config) noexcept
{
	unique_port port{::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1)};
	if (!port)
	{
		return unexpected{pal::this_thread::last_system_error()};
	}

	auto *self = new (std::nothrow) iocp_loop{};
	if (self == nullptr)
	{
		return make_unexpected(std::errc::not_enough_memory);
	}

	self->port = std::move(port);
	self->poll_fn = &iocp_poll;
	self->drain_fn = &iocp_drain_socket;
	self->config_ = config;

	return impl_ptr{self};
}

} // namespace pal::async::__event_loop

#endif // __pal_os_windows
