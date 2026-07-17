#include <pal/version.hpp>

#if __pal_os_linux

#include <pal/async/event_loop.hpp>
#include <pal/error.hpp>

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <new>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>

namespace pal::async
{

namespace __event_loop
{

namespace
{

struct epoll_loop: impl_type
{
	int epoll = -1;
	int wake = -1;

	~epoll_loop () noexcept
	{
		::close(wake);
		::close(epoll);
	}
};

constexpr ::timespec to_timespec (impl_type::clock::duration d) noexcept
{
	const auto wait = (d < impl_type::clock::duration::zero()) ? impl_type::clock::duration::zero() : d;
	const auto secs = std::chrono::duration_cast<std::chrono::seconds>(wait);
	const auto nsecs = std::chrono::duration_cast<std::chrono::nanoseconds>(wait - secs);
	return {
		.tv_sec = static_cast<::time_t>(secs.count()),
		.tv_nsec = static_cast<long>(nsecs.count()),
	};
}

void drain_wake_channel (epoll_loop &self) noexcept
{
	uint64_t counter{};
	std::ignore = ::read(self.wake, &counter, sizeof(counter));
	on_wake(self);
}

void epoll_poll (impl_type &base, impl_type::clock::duration timeout) noexcept
{
	auto &self = static_cast<epoll_loop &>(base);

	::timespec ts{};
	const ::timespec *tsp = nullptr;
	if (timeout != impl_type::clock::duration::max())
	{
		ts = to_timespec(timeout);
		tsp = &ts;
	}

	constexpr size_t poll_batch = 64;
	std::array<::epoll_event, poll_batch> events{};

	int r = 0;
	do
	{
		r = ::epoll_pwait2(self.epoll, events.data(), events.size(), tsp, nullptr);
	} while (r < 0 && errno == EINTR);

	for (int i = 0; i < r; ++i)
	{
		const auto &event = events[i];
		if (event.data.ptr == nullptr)
		{
			drain_wake_channel(self);
			continue;
		}
		auto &state = *static_cast<socket_state *>(event.data.ptr);
		if ((event.events & (EPOLLIN | EPOLLERR | EPOLLHUP)) != 0)
		{
			on_socket_event(state, state.receive);
		}
		if ((event.events & (EPOLLOUT | EPOLLERR | EPOLLHUP)) != 0)
		{
			on_socket_event(state, state.send);
		}
	}
}

std::error_code epoll_register_socket (impl_type &base, socket_state &state) noexcept
{
	auto &self = static_cast<epoll_loop &>(base);
	::epoll_event event{.events = EPOLLIN | EPOLLOUT | EPOLLET, .data = {.ptr = &state}};
	if (::epoll_ctl(self.epoll, EPOLL_CTL_ADD, static_cast<int>(state.handle), &event) == -1)
	{
		return {errno, std::generic_category()};
	}
	return {};
}

constexpr unsigned drain_batch = 16;
using mmsg_array = std::array<::mmsghdr, drain_batch>;
using mmsg_buffers_array = std::array<::iovec, drain_batch>;

size_t make_mmsg (socket_state::direction &d, mmsg_array &messages, mmsg_buffers_array &buffers, auto name_len) noexcept
{
	size_t count = 0;
	for (auto &t: d.pending)
	{
		if (count == drain_batch)
		{
			break;
		}
		buffers[count] = {.iov_base = t.span().data(), .iov_len = t.span().size()};
		messages[count].msg_hdr.msg_name = io(t).endpoint.data();
		messages[count].msg_hdr.msg_namelen = name_len(t);
		messages[count].msg_hdr.msg_iov = &buffers[count];
		messages[count].msg_hdr.msg_iovlen = 1;
		++count;
	}
	return count;
}

void epoll_drain_receive (epoll_loop &self, socket_state &state) noexcept
{
	auto &d = state.receive;

	mmsg_array messages{};
	mmsg_buffers_array buffers{};
	const auto count = make_mmsg(d, messages, buffers, [] (task &) { return io_state::endpoint_capacity; });

	int r = 0;
	do
	{
		r = ::recvmmsg(static_cast<int>(state.handle), messages.data(), count, 0, nullptr);
	} while (r < 0 && errno == EINTR);

	if (r < 0)
	{
		if (errno == EAGAIN || errno == EWOULDBLOCK)
		{
			d.ready = false;
		}
		else
		{
			// batch error attribution: -1 belongs to the head-of-FIFO op
			task *t = d.pending.pop();
			io(*t).ec.assign(errno, std::generic_category());
			self.ready_.push(*t);
		}
		return;
	}

	for (int i = 0; i < r; ++i)
	{
		task *t = d.pending.pop();
		auto &op = io(*t);
		op.bytes = messages[i].msg_len;
		op.endpoint_size = messages[i].msg_hdr.msg_namelen;
		op.truncated = (messages[i].msg_hdr.msg_flags & MSG_TRUNC) != 0;
		self.ready_.push(*t);
	}
	// Partial return: the flag stays set. A deferred mid-batch outcome (EAGAIN or a per-message error --
	// mmsg reports neither on the partial call) surfaces on the revisit's syscall.
}

void epoll_drain_send (epoll_loop &self, socket_state &state) noexcept
{
	auto &d = state.send;

	mmsg_array messages{};
	mmsg_buffers_array buffers{};
	const auto count = make_mmsg(d, messages, buffers, [] (task &t) { return io(t).endpoint_size; });

	int r = 0;
	do
	{
		r = ::sendmmsg(static_cast<int>(state.handle), messages.data(), count, 0);
	} while (r < 0 && errno == EINTR);

	if (r < 0)
	{
		if (errno == EAGAIN || errno == EWOULDBLOCK)
		{
			d.ready = false;
		}
		else
		{
			task *t = d.pending.pop();
			io(*t).ec.assign(errno, std::generic_category());
			self.ready_.push(*t);
		}
		return;
	}

	for (int i = 0; i < r; ++i)
	{
		task *t = d.pending.pop();
		io(*t).bytes = messages[i].msg_len;
		self.ready_.push(*t);
	}
}

void epoll_drain (impl_type &base, socket_state &state) noexcept
{
	auto &self = static_cast<epoll_loop &>(base);
	if (state.receive.actionable())
	{
		epoll_drain_receive(self, state);
	}
	if (state.send.actionable())
	{
		epoll_drain_send(self, state);
	}
}

impl_type::clock::time_point epoll_now (impl_type &) noexcept
{
	return impl_type::clock::now();
}

void epoll_wake (impl_type &base) noexcept
{
	auto &self = static_cast<epoll_loop &>(base);
	const uint64_t one = 1;
	std::ignore = ::write(self.wake, &one, sizeof(one));
}

void epoll_destroy (impl_type *base) noexcept
{
	delete static_cast<epoll_loop *>(base);
}

} // namespace

} // namespace __event_loop

result<event_loop> make_loop (const event_loop_config &config) noexcept
{
	using namespace __event_loop;

	const int epoll = ::epoll_create1(EPOLL_CLOEXEC);
	if (epoll == -1)
	{
		return unexpected{pal::this_thread::last_system_error()};
	}

	const int wake = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
	if (wake == -1)
	{
		const auto error = pal::this_thread::last_system_error();
		::close(epoll);
		return unexpected{error};
	}

	::epoll_event event{.events = EPOLLIN | EPOLLET, .data = {}};
	if (::epoll_ctl(epoll, EPOLL_CTL_ADD, wake, &event) == -1)
	{
		const auto error = pal::this_thread::last_system_error();
		::close(wake);
		::close(epoll);
		return unexpected{error};
	}

	auto *self = new (std::nothrow) epoll_loop{};
	if (self == nullptr)
	{
		::close(wake);
		::close(epoll);
		return make_unexpected(std::errc::not_enough_memory);
	}

	self->epoll = epoll;
	self->wake = wake;
	self->poll_fn = &epoll_poll;
	self->wake_fn = &epoll_wake;
	self->now_fn = &epoll_now;
	self->destroy_fn = &epoll_destroy;
	self->register_socket_fn = &epoll_register_socket;
	self->drain_fn = &epoll_drain;
	self->config_ = config;
	self->now_ = epoll_now(*self);

	return event_loop{impl_ptr{self}};
}

} // namespace pal::async

#endif // __pal_os_linux
