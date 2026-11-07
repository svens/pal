#include <pal/version.hpp>

#if __pal_os_linux

#include <pal/async/__event_loop.hpp>
#include <pal/error.hpp>

#include <cerrno>
#include <cstddef>
#include <new>
#include <span>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <utility>

namespace pal::async::__event_loop
{

void impl_type::poll (clock::duration timeout) noexcept
{
	::timespec ts{};
	const auto *tsp = to_timespec(timeout, ts);

	int r = 0;
	do
	{
		r = ::epoll_pwait2(
			epoll,
			events.data(),
			static_cast<int>(config_.poll_batch),
			tsp,
			nullptr
		);
	} while (r < 0 && errno == EINTR);

	if (r <= 0)
	{
		return;
	}

	for (const auto &event: std::span{events}.first(static_cast<size_t>(r)))
	{
		if (event.data.ptr == nullptr)
		{
			wake.drain(*this);
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

std::error_code impl_type::register_socket (socket_state &s) const noexcept
{
	::epoll_event event{.events = EPOLLIN | EPOLLOUT | EPOLLET, .data = {.ptr = &s}};
	if (::epoll_ctl(epoll, EPOLL_CTL_ADD, static_cast<int>(s.handle), &event) == -1)
	{
		return pal::this_thread::last_system_error();
	}
	return {};
}

namespace
{

unsigned make_mmsg (impl_type &loop, socket_state::direction &d, bool send) noexcept
{
	unsigned count = 0;
	for (auto &t: d.pending)
	{
		if (count == loop.config_.drain_batch)
		{
			break;
		}
		loop.buffers[count] = {.iov_base = t.span().data(), .iov_len = t.span().size()};
		loop.messages[count].msg_hdr.msg_name = io(t).endpoint.data();
		loop.messages[count].msg_hdr.msg_namelen = send ? io(t).endpoint_size : io_state::endpoint_capacity;
		++count;
	}
	return count;
}

} // namespace

void impl_type::drain (socket_state &s, socket_state::direction &d) noexcept
{
	const bool send = &d == &s.send;
	const auto count = make_mmsg(*this, d, send);

	int r = 0;
	do
	{
		r = send
			? ::sendmmsg(static_cast<int>(s.handle), messages.data(), count, 0)
			: ::recvmmsg(static_cast<int>(s.handle), messages.data(), count, 0, nullptr);
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

result<impl_ptr> make_backend (const event_loop_config &config) noexcept
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

	impl_ptr self{new (std::nothrow) impl_type{}};
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
	self->config_ = config;

	return self;
}

} // namespace pal::async::__event_loop

#endif // __pal_os_linux
