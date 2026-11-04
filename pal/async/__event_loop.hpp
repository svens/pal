#pragma once

/**
 * \file pal/async/__event_loop.hpp
 * Cross-TU internal seam for pal/async/event_loop backends (internal)
 */

#include <pal/async/event_loop.hpp>
#include <pal/async/multishot.hpp>
#include <pal/error.hpp>
#include <pal/version.hpp>

#include <cstddef>
#include <new>
#include <span>
#include <tuple>
#include <utility>

#if __pal_os_linux || __pal_os_macos
	#include <array>
	#include <bit>
	#include <cerrno>
	#include <chrono>
	#include <cstdint>
	#include <fcntl.h>
	#include <time.h>
	#include <unistd.h>
#endif

namespace pal::async::__event_loop
{

void on_socket_event (socket_state &s, socket_state::direction &d) noexcept;

inline constexpr size_t multishot_pool_header_size = __task::round_up(sizeof(multishot_pool), cache_line_size);

inline std::byte *slot_address (multishot_pool &pool, size_t index) noexcept
{
	return reinterpret_cast<std::byte *>(&pool) + multishot_pool_header_size + index * pool.slot_size;
}

inline task &slot (multishot_pool &pool, size_t index) noexcept
{
	return *std::launder(reinterpret_cast<task *>(slot_address(pool, index)));
}

inline std::span<std::byte> headroom_and_buffer (const multishot_pool &pool, task &t) noexcept
{
	return {reinterpret_cast<std::byte *>(&t) + sizeof(task), pool.headroom + pool.buffer_size};
}

void prime_multishot (multishot_pool &pool, task &t) noexcept;
void feed_multishot (multishot_pool &pool, task &t) noexcept;

result<impl_ptr> make_epoll_loop (const event_loop_config &config) noexcept;
result<impl_ptr> make_io_uring_loop (const event_loop_config &config) noexcept;
result<impl_ptr> make_kqueue_loop (const event_loop_config &config) noexcept;
result<impl_ptr> make_iocp_loop (const event_loop_config &config) noexcept;

// make_loop's body, with the io_uring probe replaceable by a compile-time failure so the fallback to
// epoll is a tested path on machines where io_uring works.
template <bool inject_probe_failure>
result<event_loop> make_loop_impl (const event_loop_config &config) noexcept;

extern template result<event_loop> make_loop_impl<false> (const event_loop_config &) noexcept;
extern template result<event_loop> make_loop_impl<true> (const event_loop_config &) noexcept;

template <typename T, auto Close, T Invalid>
class unique_handle
{
public:

	unique_handle () = default;

	explicit unique_handle (T value) noexcept
		: value_{value}
	{
	}

	unique_handle (unique_handle &&that) noexcept
		: value_{std::exchange(that.value_, Invalid)}
	{
	}

	unique_handle &operator= (unique_handle &&that) noexcept
	{
		std::swap(value_, that.value_);
		return *this;
	}

	unique_handle (const unique_handle &) = delete;
	unique_handle &operator= (const unique_handle &) = delete;

	~unique_handle () noexcept
	{
		if (value_ != Invalid)
		{
			std::ignore = Close(value_);
		}
	}

	operator T () const noexcept
	{
		return value_;
	}

	explicit operator bool () const noexcept
	{
		return value_ != Invalid;
	}

private:

	T value_ = Invalid;
};

#if __pal_os_linux || __pal_os_macos

constexpr ::timespec to_timespec (impl_type::clock::duration d) noexcept
{
	const auto wait = (d < impl_type::clock::duration::zero()) ? impl_type::clock::duration::zero() : d;
	const auto secs = std::chrono::duration_cast<std::chrono::seconds>(wait);
	const auto nsecs = std::chrono::duration_cast<std::chrono::nanoseconds>(wait - secs);
	return
	{
		.tv_sec = static_cast<::time_t>(secs.count()),
		.tv_nsec = static_cast<long>(nsecs.count()),
	};
}

using unique_fd = unique_handle<int, ::close, -1>;

struct wake_channel
{
	unique_fd read{};
	unique_fd write{};

	static result<wake_channel> make () noexcept
	{
		int fd[2];
		#if __pal_os_linux
		{
			if (::pipe2(fd, O_CLOEXEC) == -1)
			{
				return unexpected{pal::this_thread::last_system_error()};
			}
		}
		#else
		{
			if (::pipe(fd) == -1)
			{
				return unexpected{pal::this_thread::last_system_error()};
			}
			::fcntl(fd[0], F_SETFD, ::fcntl(fd[0], F_GETFD, 0) | FD_CLOEXEC);
			::fcntl(fd[1], F_SETFD, ::fcntl(fd[1], F_GETFD, 0) | FD_CLOEXEC);
		}
		#endif
		::fcntl(fd[0], F_SETFL, ::fcntl(fd[0], F_GETFL, 0) | O_NONBLOCK);

		return wake_channel
		{
			.read = unique_fd{fd[0]},
			.write = unique_fd{fd[1]},
		};
	}

	void post (task &t) const noexcept
	{
		const auto p = std::bit_cast<std::uintptr_t>(&t);
		ssize_t r = 0;
		do
		{
			r = ::write(write, &p, sizeof(p));
		} while (r < 0 && errno == EINTR);
	}

	void drain (impl_type &l) const noexcept
	{
		std::array<std::uintptr_t, 256> tasks;
		size_t total = 0;
		for (;;)
		{
			if (auto r = ::read(read, tasks.data(), sizeof(tasks)); r > 0)
			{
				r /= sizeof(std::uintptr_t);
				for (ssize_t i = 0; i < r; ++i)
				{
					l.posted_.push(*std::bit_cast<task *>(tasks[i]));
				}
				total += static_cast<size_t>(r);
			}
			else if (r == 0 || errno != EINTR)
			{
				break;
			}
		}
		if (total > 0)
		{
			l.posts_in_flight_.fetch_sub(total, std::memory_order_relaxed);
		}
	}
};

struct posix_loop: impl_type
{
	wake_channel wake{};

	void do_post (task &t) noexcept override
	{
		wake.post(t);
	}
};

#endif

} // namespace pal::async::__event_loop
