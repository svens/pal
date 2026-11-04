#include <pal/version.hpp>

#if __pal_os_linux

#include <pal/async/__event_loop.hpp>
#include <pal/error.hpp>

#include <array>
#include <cerrno>
#include <new>
#include <span>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <utility>

namespace pal::async::__event_loop
{

namespace
{

using poll_array = std::array<::epoll_event, event_loop_config::max_poll_batch>;
using mmsg_array = std::array<::mmsghdr, event_loop_config::max_drain_batch>;
using mmsg_buffers_array = std::array<::iovec, event_loop_config::max_drain_batch>;

struct epoll_loop: posix_loop
{
	unique_fd epoll{};
	poll_array events{};
	mmsg_array messages{};
	mmsg_buffers_array buffers{};

	std::error_code register_socket (socket_state &state) noexcept override
	{
		::epoll_event event{.events = EPOLLIN | EPOLLOUT | EPOLLET, .data = {.ptr = &state}};
		if (::epoll_ctl(epoll, EPOLL_CTL_ADD, static_cast<int>(state.handle), &event) == -1)
		{
			return {errno, std::generic_category()};
		}
		return {};
	}

	unsigned make_mmsg (socket_state::direction &d, bool send) noexcept;
	void try_socket_op (socket_state &state, socket_state::direction &d) noexcept;
};

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

	int r = 0;
	do
	{
		r = ::epoll_pwait2(
			self.epoll,
			self.events.data(),
			static_cast<int>(self.config_.poll_batch),
			tsp,
			nullptr
		);
	} while (r < 0 && errno == EINTR);

	if (r <= 0)
	{
		return;
	}

	for (const auto &event: std::span{self.events}.first(static_cast<size_t>(r)))
	{
		if (event.data.ptr == nullptr)
		{
			self.wake.drain(self);
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

unsigned epoll_loop::make_mmsg (socket_state::direction &d, bool send) noexcept
{
	unsigned count = 0;
	for (auto &t: d.pending)
	{
		if (count == config_.drain_batch)
		{
			break;
		}
		buffers[count] = {.iov_base = t.span().data(), .iov_len = t.span().size()};
		messages[count].msg_hdr.msg_name = io(t).endpoint.data();
		messages[count].msg_hdr.msg_namelen = send ? io(t).endpoint_size : io_state::endpoint_capacity;
		++count;
	}
	return count;
}

void epoll_loop::try_socket_op (socket_state &state, socket_state::direction &d) noexcept
{
	const bool send = &d == &state.send;
	const auto count = make_mmsg(d, send);

	int r = 0;
	do
	{
		r = send
			? ::sendmmsg(static_cast<int>(state.handle), messages.data(), count, 0)
			: ::recvmmsg(static_cast<int>(state.handle), messages.data(), count, 0, nullptr);
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
			io(*t).native_error = errno;
			ready_.push(*t);
		}
		return;
	}

	for (int i = 0; i < r; ++i)
	{
		task *t = d.pending.pop();
		auto &op = io(*t);
		op.bytes = messages[i].msg_len;
		if (!send)
		{
			op.endpoint_size = messages[i].msg_hdr.msg_namelen;
			op.truncated = (messages[i].msg_hdr.msg_flags & MSG_TRUNC) != 0;
		}
		ready_.push(*t);
	}
	// Partial return: the flag stays set. A deferred mid-batch outcome (EAGAIN or a per-message error --
	// mmsg reports neither on the partial call) surfaces on the revisit's syscall.
}

void epoll_drain_socket (impl_type &base, socket_state &state, socket_state::direction &d) noexcept
{
	static_cast<epoll_loop &>(base).try_socket_op(state, d);
}

} // namespace

result<impl_ptr> make_epoll_loop (const event_loop_config &config) noexcept
{
	unique_fd epoll{::epoll_create1(EPOLL_CLOEXEC)};
	if (!epoll)
	{
		return unexpected{pal::this_thread::last_system_error()};
	}

	auto wake = wake_channel::make();
	if (!wake)
	{
		return unexpected{wake.error()};
	}

	::epoll_event event{.events = EPOLLIN | EPOLLET, .data = {}};
	if (::epoll_ctl(epoll, EPOLL_CTL_ADD, wake->read, &event) == -1)
	{
		return unexpected{pal::this_thread::last_system_error()};
	}

	auto *self = new (std::nothrow) epoll_loop{};
	if (self == nullptr)
	{
		return make_unexpected(std::errc::not_enough_memory);
	}

	for (size_t i = 0; i < config.drain_batch; ++i)
	{
		self->messages[i].msg_hdr.msg_iov = &self->buffers[i];
		self->messages[i].msg_hdr.msg_iovlen = 1;
	}

	self->epoll = std::move(epoll);
	self->wake = std::move(*wake);
	self->poll_fn = &epoll_poll;
	self->drain_fn = &epoll_drain_socket;
	self->requeue_multishot_fn = &feed_multishot;
	self->config_ = config;

	return impl_ptr{self};
}

} // namespace pal::async::__event_loop

#endif // __pal_os_linux
