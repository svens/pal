#pragma once

/**
 * \file pal/intrusive_mpsc_queue.hpp
 * Intrusive lock-free MPSC queue (FIFO)
 */

#include <pal/__diagnostic.hpp>
#include <atomic>
#include <cstddef>

namespace pal
{

/// Intrusive MPSC queue hook.
/// \see intrusive_mpsc_queue
template <typename T>
using intrusive_mpsc_queue_hook = std::atomic<T *>;

/// \cond
template <auto Next>
class intrusive_mpsc_queue;
/// \endcond

/// Intrusive multiple-producers single-consumer lock-free queue (MPSC FIFO).
///
/// Elements of this container must provide member \a Next that stores opaque
/// data managed by the container. At any given time a specific hook can be
/// used only to store an element in a single container. The same hook can be
/// reused in different containers at different times. If an element needs to
/// be stored in multiple containers simultaneously, it must provide multiple
/// hooks, one per owner.
///
/// Being intrusive, the container does not manage node allocation. It is the
/// caller's responsibility to keep nodes alive while they are in the container
/// and to not interfere with their hooks.
///
/// Usage:
/// \code
/// struct foo
/// {
///   pal::intrusive_mpsc_queue_hook<foo> hook{};
///   using queue = pal::intrusive_mpsc_queue<&foo::hook>;
/// };
///
/// foo::queue q;
/// foo f;
/// q.push(f);
/// foo *fp = q.try_pop(); // fp == &f
/// \endcode
///
/// \note push() is safe to call concurrently from multiple threads.
/// try_pop() and empty() must only be called from a single consumer thread.
/// Items from the same producer are delivered in push order; items from
/// different producers may interleave in any order.
///
template <typename T, typename Hook, Hook T::*Next>
class intrusive_mpsc_queue<Next>
{
public:

	/// Element type of this container.
	using value_type = T;

	intrusive_mpsc_queue () noexcept = default;
	~intrusive_mpsc_queue () noexcept = default;

	intrusive_mpsc_queue (const intrusive_mpsc_queue &) = delete;
	intrusive_mpsc_queue &operator= (const intrusive_mpsc_queue &) = delete;

	/// Push \a node onto the back of the queue. Thread-safe. All writes to
	/// \a node before push() happen-before any reads from the returned node
	/// after try_pop().
	void push (value_type &node) noexcept
	{
		(node.*Next).store(nullptr, std::memory_order_relaxed);
		auto *back = tail_.exchange(&node, std::memory_order_release);
		(back->*Next).store(&node, std::memory_order_release);
	}

	/// Remove and return the front node. Returns nullptr if empty or transiently
	/// inconsistent (a producer is mid-push); the caller should retry.
	/// Consumer thread only.
	[[nodiscard]] value_type *try_pop () noexcept
	{
		auto *front = head_;
		auto *next = (front->*Next).load(std::memory_order_acquire);

		if (front == sentry_)
		{
			if (next == nullptr)
			{
				return nullptr;
			}
			front = head_ = next;
			next = (front->*Next).load(std::memory_order_acquire);
		}

		if (next)
		{
			head_ = next;
			return front;
		}

		if (front != tail_.load(std::memory_order_acquire))
		{
			return nullptr;
		}

		push(*sentry_);

		next = (front->*Next).load(std::memory_order_acquire);
		if (next)
		{
			head_ = next;
			return front;
		}

		return nullptr;
	}

	/// Return true if the queue has no elements. Consumer thread only.
	[[nodiscard]] bool empty () const noexcept
	{
		return tail_.load(std::memory_order_acquire) == sentry_;
	}

private:

	alignas(alignof(value_type)) std::byte stub_[sizeof(value_type)]{};
	value_type *const sentry_{reinterpret_cast<value_type *>(stub_)};

	// std::hardware_destructive_interference_size reflects the build host, not
	// the target, so it cannot be used in public ABI
	static constexpr size_t cache_line_size = 128;

	// clang-format off
	__pal_diagnostic(push)
	__pal_diagnostic_suppress(__pal_aligned_struct_padding)

	alignas(cache_line_size) std::atomic<value_type *> tail_{sentry_};
	alignas(cache_line_size) value_type *head_{sentry_};

	__pal_diagnostic (pop)
	// clang-format on
};

} // namespace pal
