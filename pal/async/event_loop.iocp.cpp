#include <pal/version.hpp>

#if __pal_os_windows

#include <pal/async/event_loop.hpp>
#include <pal/error.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <new>
#include <tuple>
#include <winsock2.h>
#include <windows.h>

namespace pal::async::__event_loop
{

namespace
{

using poll_array = std::array<::OVERLAPPED_ENTRY, event_loop_config::max_poll_batch>;

struct iocp_loop: impl_type
{
	::HANDLE port = nullptr;
	poll_array events{};

	~iocp_loop () noexcept
	{
		::CloseHandle(port);
	}
};

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

void harvest (iocp_loop &self, const ::OVERLAPPED_ENTRY &entry) noexcept
{
	if (entry.lpCompletionKey == wake_key)
	{
		on_wake(self);
		return;
	}

	auto &state = *reinterpret_cast<socket_state *>(entry.lpCompletionKey);
	auto &n = native(entry.lpOverlapped);
	auto &t = *n.self;
	auto &op = io(t);

	--state.native_in_flight;

	if (entry.lpOverlapped->Internal == 0)
	{
		on_success(op, n, entry.dwNumberOfBytesTransferred);
	}
	else
	{
		on_failure(op, n, state);
	}

	self.ready_.push(t);
}

void iocp_poll (impl_type &base, impl_type::clock::duration timeout) noexcept
{
	auto &self = static_cast<iocp_loop &>(base);

	::DWORD ms = INFINITE;
	if (timeout != (impl_type::clock::duration::max)())
	{
		ms = to_milliseconds(timeout);
	}

	::ULONG count = 0;
	std::ignore = ::GetQueuedCompletionStatusEx(
		self.port,
		self.events.data(),
		static_cast<::ULONG>(self.config_.poll_batch),
		&count,
		ms,
		FALSE
	);

	for (::ULONG i = 0; i < count; ++i)
	{
		harvest(self, self.events[i]);
	}
}

std::error_code iocp_register_socket (impl_type &base, socket_state &state) noexcept
{
	auto &self = static_cast<iocp_loop &>(base);

	const auto key = reinterpret_cast<::ULONG_PTR>(&state);
	if (::CreateIoCompletionPort(to_handle(state), self.port, key, 0) == nullptr)
	{
		return pal::this_thread::last_system_error();
	}

	return {};
}

void iocp_submit_socket_op (impl_type &base, task &t, socket_state &state, socket_state::direction &d) noexcept
{
	auto &self = static_cast<iocp_loop &>(base);
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
		self.ready_.push(t);
		return;
	}

	const auto error = ::WSAGetLastError();
	if (error == WSA_IO_PENDING)
	{
		++state.native_in_flight;
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
	self.ready_.push(t);
}

// Drains synchronously so every op of a destroyed socket is queued before the destructor returns, which a
// readiness backend gets for free by unqueueing them.
void iocp_close_socket (impl_type &base, socket_state &state) noexcept
{
	auto &self = static_cast<iocp_loop &>(base);

	if (state.native_in_flight > 0)
	{
		std::ignore = ::CancelIoEx(to_handle(state), nullptr);

		std::array<::OVERLAPPED_ENTRY, 64> events{};
		while (state.native_in_flight > 0)
		{
			::ULONG count = 0;
			std::ignore = ::GetQueuedCompletionStatusEx(
				self.port,
				events.data(),
				static_cast<::ULONG>(events.size()),
				&count,
				INFINITE,
				FALSE
			);

			for (::ULONG i = 0; i < count; ++i)
			{
				harvest(self, events[i]);
			}
		}
	}

	close_socket(base, state);
}

impl_type::clock::time_point iocp_now (impl_type &) noexcept
{
	return impl_type::clock::now();
}

void iocp_wake (impl_type &base) noexcept
{
	auto &self = static_cast<iocp_loop &>(base);
	std::ignore = ::PostQueuedCompletionStatus(self.port, 0, wake_key, nullptr);
}

void iocp_destroy (impl_type *base) noexcept
{
	delete static_cast<iocp_loop *>(base);
}

} // namespace

result<impl_ptr> make_iocp_loop (const event_loop_config &config) noexcept
{
	::HANDLE port = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1);
	if (port == nullptr)
	{
		return unexpected{pal::this_thread::last_system_error()};
	}

	auto *self = new (std::nothrow) iocp_loop{};
	if (self == nullptr)
	{
		::CloseHandle(port);
		return make_unexpected(std::errc::not_enough_memory);
	}

	self->port = port;
	self->poll_fn = &iocp_poll;
	self->wake_fn = &iocp_wake;
	self->now_fn = &iocp_now;
	self->destroy_fn = &iocp_destroy;
	self->register_socket_fn = &iocp_register_socket;
	self->submit_socket_op_fn = &iocp_submit_socket_op;
	self->close_socket_fn = &iocp_close_socket;
	self->config_ = config;
	self->now_ = iocp_now(*self);

	return impl_ptr{self};
}

} // namespace pal::async::__event_loop

#endif // __pal_os_windows
