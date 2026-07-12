#include <pal/version.hpp>

#if __pal_os_windows

#include <pal/async/event_loop.hpp>
#include <pal/error.hpp>

#include <atomic>
#include <chrono>
#include <new>
#include <windows.h>

namespace pal::async
{

namespace __event_loop
{

namespace
{

struct iocp_loop: impl_type
{
	::HANDLE port = nullptr;
	std::atomic<bool> signaled = false;

	~iocp_loop () noexcept
	{
		::CloseHandle(port);
	}
};

constexpr ::ULONG_PTR wake_key = 1;

constexpr ::DWORD to_milliseconds (impl_type::clock::duration d) noexcept
{
	const auto wait = (d < impl_type::clock::duration::zero()) ? impl_type::clock::duration::zero() : d;
	const auto msecs = std::chrono::ceil<std::chrono::milliseconds>(wait).count();
	constexpr auto cap = static_cast<std::chrono::milliseconds::rep>(INFINITE) - 1;
	return static_cast<::DWORD>((msecs > cap) ? cap : msecs);
}

size_t iocp_poll (impl_type &base, impl_type::clock::duration timeout) noexcept
{
	auto &self = static_cast<iocp_loop &>(base);

	::DWORD ms = INFINITE;
	if (timeout != (impl_type::clock::duration::max)())
	{
		ms = to_milliseconds(timeout);
	}

	::OVERLAPPED_ENTRY entry{};
	::ULONG count = 0;
	if (::GetQueuedCompletionStatusEx(self.port, &entry, 1, &count, ms, FALSE) && count > 0)
	{
		if (entry.lpCompletionKey == wake_key)
		{
			self.signaled.store(false, std::memory_order_release);
			self.stats_.wakeups++;
		}
	}

	return 0;
}

impl_type::clock::time_point iocp_now (impl_type &) noexcept
{
	return impl_type::clock::now();
}

void iocp_wake (impl_type &base) noexcept
{
	auto &self = static_cast<iocp_loop &>(base);
	if (!self.signaled.exchange(true, std::memory_order_acq_rel))
	{
		std::ignore = ::PostQueuedCompletionStatus(self.port, 0, wake_key, nullptr);
	}
}

void iocp_destroy (impl_type *base) noexcept
{
	delete static_cast<iocp_loop *>(base);
}

} // namespace

} // namespace __event_loop

result<event_loop> make_loop (const event_loop_config &config) noexcept
{
	using namespace __event_loop;

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
	self->config_ = config;
	self->now_ = iocp_now(*self);

	return event_loop{impl_ptr{self}};
}

} // namespace pal::async

#endif // __pal_os_windows
