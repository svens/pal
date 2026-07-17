#include <pal/version.hpp>

#if __pal_os_macos

#include <pal/async/event_loop.hpp>
#include <pal/error.hpp>

#include <array>
#include <cerrno>
#include <chrono>
#include <fcntl.h>
#include <new>
#include <sys/event.h>
#include <sys/socket.h>
#include <unistd.h>

namespace pal::async
{

namespace __event_loop
{

namespace
{

struct kqueue_loop: impl_type
{
	int kq = -1;

	~kqueue_loop () noexcept
	{
		::close(kq);
	}
};

constexpr uintptr_t wake_ident = 1;

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

constexpr size_t poll_batch = 64;
constexpr size_t drain_batch = 16;

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

	std::array<struct ::kevent, poll_batch> events{};
	int r = 0;
	do
	{
		r = ::kevent(self.kq, nullptr, 0, events.data(), static_cast<int>(events.size()), tsp);
	} while (r < 0 && errno == EINTR);

	for (int i = 0; i < r; ++i)
	{
		const auto &event = events[i];
		if (event.filter == EVFILT_USER)
		{
			on_wake(self);
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

std::error_code kqueue_register_socket (impl_type &base, socket_state &state) noexcept
{
	auto &self = static_cast<kqueue_loop &>(base);
	std::array<struct ::kevent, 2> reg{};
	EV_SET(reg.data(), static_cast<uintptr_t>(state.handle), EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, &state);
	EV_SET(&reg[1], static_cast<uintptr_t>(state.handle), EVFILT_WRITE, EV_ADD | EV_CLEAR, 0, 0, &state);
	if (::kevent(self.kq, reg.data(), static_cast<int>(reg.size()), nullptr, 0, nullptr) == -1)
	{
		return {errno, std::generic_category()};
	}
	return {};
}

void kqueue_drain_receive (kqueue_loop &self, socket_state &state) noexcept
{
	auto &d = state.receive;
	for (size_t i = 0; i < drain_batch && d.actionable(); ++i)
	{
		auto &t = *d.pending.head();
		auto &op = io(t);

		::iovec buffer{.iov_base = t.span().data(), .iov_len = t.span().size()};
		::msghdr message{};
		message.msg_name = op.endpoint.data();
		message.msg_namelen = io_state::endpoint_capacity;
		message.msg_iov = &buffer;
		message.msg_iovlen = 1;

		ssize_t r = 0;
		do
		{
			r = ::recvmsg(static_cast<int>(state.handle), &message, 0);
		} while (r < 0 && errno == EINTR);

		if (r < 0)
		{
			if (errno == EAGAIN || errno == EWOULDBLOCK)
			{
				d.ready = false;
				return;
			}
			op.ec.assign(errno, std::generic_category());
		}
		else
		{
			op.bytes = static_cast<size_t>(r);
			op.endpoint_size = message.msg_namelen;
			op.truncated = (message.msg_flags & MSG_TRUNC) != 0;
		}
		self.ready_.push(*d.pending.pop());
	}
}

void kqueue_drain_send (kqueue_loop &self, socket_state &state) noexcept
{
	auto &d = state.send;
	for (size_t i = 0; i < drain_batch && d.actionable(); ++i)
	{
		auto &t = *d.pending.head();
		auto &op = io(t);

		::iovec buffer{.iov_base = t.span().data(), .iov_len = t.span().size()};
		::msghdr message{};
		message.msg_name = op.endpoint.data();
		message.msg_namelen = op.endpoint_size;
		message.msg_iov = &buffer;
		message.msg_iovlen = 1;

		ssize_t r = 0;
		do
		{
			r = ::sendmsg(static_cast<int>(state.handle), &message, 0);
		} while (r < 0 && errno == EINTR);

		if (r < 0)
		{
			if (errno == EAGAIN || errno == EWOULDBLOCK)
			{
				d.ready = false;
				return;
			}
			op.ec.assign(errno, std::generic_category());
		}
		else
		{
			op.bytes = static_cast<size_t>(r);
		}
		self.ready_.push(*d.pending.pop());
	}
}

void kqueue_drain (impl_type &base, socket_state &state) noexcept
{
	auto &self = static_cast<kqueue_loop &>(base);
	if (state.receive.actionable())
	{
		kqueue_drain_receive(self, state);
	}
	if (state.send.actionable())
	{
		kqueue_drain_send(self, state);
	}
}

impl_type::clock::time_point kqueue_now (impl_type &) noexcept
{
	return impl_type::clock::now();
}

void kqueue_wake (impl_type &base) noexcept
{
	auto &self = static_cast<kqueue_loop &>(base);
	struct ::kevent trigger{};
	EV_SET(&trigger, wake_ident, EVFILT_USER, 0, NOTE_TRIGGER, 0, nullptr);
	std::ignore = ::kevent(self.kq, &trigger, 1, nullptr, 0, nullptr);
}

void kqueue_destroy (impl_type *base) noexcept
{
	delete static_cast<kqueue_loop *>(base);
}

} // namespace

} // namespace __event_loop

result<event_loop> make_loop (const event_loop_config &config) noexcept
{
	using namespace __event_loop;

	const int kq = ::kqueue();
	if (kq == -1)
	{
		return unexpected{pal::this_thread::last_system_error()};
	}
	::fcntl(kq, F_SETFD, ::fcntl(kq, F_GETFD, 0) | FD_CLOEXEC);

	struct ::kevent reg{};
	EV_SET(&reg, wake_ident, EVFILT_USER, EV_ADD | EV_CLEAR, 0, 0, nullptr);
	if (::kevent(kq, &reg, 1, nullptr, 0, nullptr) == -1)
	{
		const auto error = pal::this_thread::last_system_error();
		::close(kq);
		return unexpected{error};
	}

	auto *self = new (std::nothrow) kqueue_loop{};
	if (self == nullptr)
	{
		::close(kq);
		return make_unexpected(std::errc::not_enough_memory);
	}

	self->kq = kq;
	self->poll_fn = &kqueue_poll;
	self->wake_fn = &kqueue_wake;
	self->now_fn = &kqueue_now;
	self->destroy_fn = &kqueue_destroy;
	self->register_socket_fn = &kqueue_register_socket;
	self->drain_fn = &kqueue_drain;
	self->config_ = config;
	self->now_ = kqueue_now(*self);

	return event_loop{impl_ptr{self}};
}

} // namespace pal::async

#endif // __pal_os_macos
