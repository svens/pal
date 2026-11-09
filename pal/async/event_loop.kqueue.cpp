#include <pal/version.hpp>

#if __pal_os_macos

#include <pal/async/__event_loop.hpp>
#include <pal/error.hpp>

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <new>
#include <span>
#include <sys/event.h>
#include <sys/socket.h>
#include <utility>

namespace pal::async::__event_loop
{

std::error_code impl_type::poll (clock::duration timeout) noexcept
{
	::timespec ts{};
	const auto *tsp = to_timespec(timeout, ts);

	int r = 0;
	do
	{
		r = ::kevent(
			kq,
			nullptr,
			0,
			events.data(),
			static_cast<int>(config_.poll_batch),
			tsp
		);
	} while (r < 0 && errno == EINTR);

	if (r < 0)
	{
		return pal::this_thread::last_system_error();
	}

	for (const auto &event: std::span{events}.first(static_cast<size_t>(r)))
	{
		if (event.udata == nullptr)
		{
			wake.drain(*this);
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
	return {};
}

std::error_code impl_type::register_socket (socket_state &s) const noexcept
{
	std::array<struct ::kevent, 2> reg{};
	EV_SET(&reg[0], static_cast<uintptr_t>(s.handle), EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, &s);
	EV_SET(&reg[1], static_cast<uintptr_t>(s.handle), EVFILT_WRITE, EV_ADD | EV_CLEAR, 0, 0, &s);
	if (::kevent(kq, reg.data(), static_cast<int>(reg.size()), nullptr, 0, nullptr) == -1)
	{
		return pal::this_thread::last_system_error();
	}
	return {};
}

namespace
{

void drain_datagram (impl_type &loop, socket_state &s, socket_state::direction &d) noexcept
{
	const bool send = &d == &s.send;
	for (size_t i = 0; i < loop.config_.drain_batch && d.actionable(); ++i)
	{
		auto &t = *d.pending.head();
		auto &op = io(t);

		::iovec buffer{.iov_base = t.span().data(), .iov_len = t.span().size()};
		::msghdr message{};
		message.msg_name = (op.endpoint_size != 0) ? op.endpoint.data() : nullptr;
		message.msg_namelen = op.endpoint_size;
		message.msg_iov = &buffer;
		message.msg_iovlen = 1;

		ssize_t r = 0;
		do
		{
			r = send
				? ::sendmsg(static_cast<int>(s.handle), &message, 0)
				: ::recvmsg(static_cast<int>(s.handle), &message, 0);
		} while (r < 0 && errno == EINTR);

		if (r < 0)
		{
			static_assert(EAGAIN == EWOULDBLOCK);
			if (errno == EAGAIN)
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
		loop.ready_.push(*d.pending.pop());
	}
}

} // namespace

void impl_type::drain (socket_state &s, socket_state::direction &d) noexcept
{
	switch (s.kind)
	{
		case socket_kind::datagram:
			drain_datagram(*this, s, d);
			return;
		case socket_kind::stream:
			drain_stream(*this, s, d);
			return;
	}
}

result<impl_ptr> make_backend (const event_loop_config &config) noexcept
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

	impl_ptr self{new (std::nothrow) impl_type{}};
	if (self == nullptr)
	{
		return make_unexpected(std::errc::not_enough_memory);
	}

	self->kq = std::move(kq);
	self->wake = std::move(*wake);
	self->config_ = config;

	return self;
}

} // namespace pal::async::__event_loop

#endif // __pal_os_macos
