#include <pal/version.hpp>

#if __pal_os_macos

#include <pal/async/__event_loop.hpp>
#include <pal/error.hpp>

#include <array>
#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <new>
#include <span>
#include <sys/event.h>
#include <sys/socket.h>
#include <utility>

namespace pal::async::__event_loop
{

namespace
{

using poll_array = std::array<struct ::kevent, event_loop_config::max_poll_batch>;

struct kqueue_loop: posix_loop
{
	unique_fd kq{};
	poll_array events{};

	std::error_code register_socket (socket_state &state) noexcept override
	{
		std::array<struct ::kevent, 2> reg{};
		EV_SET(&reg[0], static_cast<uintptr_t>(state.handle), EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, &state);
		EV_SET(&reg[1], static_cast<uintptr_t>(state.handle), EVFILT_WRITE, EV_ADD | EV_CLEAR, 0, 0, &state);
		if (::kevent(kq, reg.data(), static_cast<int>(reg.size()), nullptr, 0, nullptr) == -1)
		{
			return {errno, std::generic_category()};
		}
		return {};
	}

	void try_socket_op (socket_state &state, socket_state::direction &d) noexcept;
};

void kqueue_poll (impl_type &base, impl_type::clock::duration timeout) noexcept
{
	auto &self = static_cast<kqueue_loop &>(base);

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
		r = ::kevent(
			self.kq,
			nullptr,
			0,
			self.events.data(),
			static_cast<int>(self.config_.poll_batch),
			tsp
		);
	} while (r < 0 && errno == EINTR);

	if (r <= 0)
	{
		return;
	}

	for (const auto &event: std::span{self.events}.first(static_cast<size_t>(r)))
	{
		if (event.udata == nullptr)
		{
			self.wake.drain(self);
		}
		else if (event.filter == EVFILT_READ)
		{
			auto &state = *static_cast<socket_state *>(event.udata);
			on_socket_event(state, state.receive);
		}
		else if (event.filter == EVFILT_WRITE)
		{
			auto &state = *static_cast<socket_state *>(event.udata);
			on_socket_event(state, state.send);
		}
	}
}

void kqueue_loop::try_socket_op (socket_state &state, socket_state::direction &d) noexcept
{
	const bool send = &d == &state.send;
	auto &t = *d.pending.head();
	auto &op = io(t);

	::iovec buffer{.iov_base = t.span().data(), .iov_len = t.span().size()};
	::msghdr message{};
	message.msg_name = op.endpoint.data();
	message.msg_namelen = send ? op.endpoint_size : io_state::endpoint_capacity;
	message.msg_iov = &buffer;
	message.msg_iovlen = 1;

	ssize_t r = 0;
	do
	{
		r = send
			? ::sendmsg(static_cast<int>(state.handle), &message, 0)
			: ::recvmsg(static_cast<int>(state.handle), &message, 0);
	} while (r < 0 && errno == EINTR);

	if (r < 0)
	{
		if (errno == EAGAIN || errno == EWOULDBLOCK)
		{
			d.ready = false;
			return;
		}
		op.native_error = errno;
	}
	else
	{
		op.bytes = static_cast<size_t>(r);
		if (!send)
		{
			op.endpoint_size = message.msg_namelen;
			op.truncated = (message.msg_flags & MSG_TRUNC) != 0;
		}
	}
	ready_.push(*d.pending.pop());
}

void kqueue_drain_socket (impl_type &base, socket_state &state, socket_state::direction &d) noexcept
{
	auto &self = static_cast<kqueue_loop &>(base);
	for (size_t i = 0; i < self.config_.drain_batch && d.actionable(); ++i)
	{
		self.try_socket_op(state, d);
	}
}

} // namespace

result<impl_ptr> make_kqueue_loop (const event_loop_config &config) noexcept
{
	unique_fd kq{::kqueue()};
	if (!kq)
	{
		return unexpected{pal::this_thread::last_system_error()};
	}
	::fcntl(kq, F_SETFD, ::fcntl(kq, F_GETFD, 0) | FD_CLOEXEC);

	auto wake = wake_channel::make();
	if (!wake)
	{
		return unexpected{wake.error()};
	}

	struct ::kevent reg{};
	EV_SET(&reg, static_cast<uintptr_t>(wake->read), EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, nullptr);
	if (::kevent(kq, &reg, 1, nullptr, 0, nullptr) == -1)
	{
		return unexpected{pal::this_thread::last_system_error()};
	}

	auto *self = new (std::nothrow) kqueue_loop{};
	if (self == nullptr)
	{
		return make_unexpected(std::errc::not_enough_memory);
	}

	self->kq = std::move(kq);
	self->wake = std::move(*wake);
	self->poll_fn = &kqueue_poll;
	self->drain_fn = &kqueue_drain_socket;
	self->requeue_multishot_fn = &feed_multishot;
	self->config_ = config;

	return impl_ptr{self};
}

} // namespace pal::async::__event_loop

#endif // __pal_os_macos
