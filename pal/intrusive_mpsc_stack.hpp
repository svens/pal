#pragma once

/**
 * \file pal/intrusive_mpsc_stack.hpp
 * Intrusive lock-free MPSC stack (LIFO)
 */

#include <atomic>

namespace pal
{

/// Intrusive MPSC stack hook.
/// \see intrusive_mpsc_stack
template <typename T>
using intrusive_mpsc_stack_hook = std::atomic<T *>;

/// \cond
template <auto Next>
class intrusive_mpsc_stack;
/// \endcond

/// Intrusive multiple-producers single-consumer lock-free stack (MPSC LIFO).
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
///   pal::intrusive_mpsc_stack_hook<foo> hook{};
///   using stack = pal::intrusive_mpsc_stack<&foo::hook>;
/// };
///
/// foo::stack s;
/// foo f;
/// s.push(f);
/// foo *fp = s.try_pop(); // fp == &f
/// \endcode
///
/// \note push() is safe to call concurrently from multiple producer threads.
/// try_pop() and empty() must only be called from the single consumer thread.
///
template <typename T, typename Hook, Hook T::*Next>
class intrusive_mpsc_stack<Next>
{
public:

	/// Element type of this container.
	using value_type = T;

	intrusive_mpsc_stack () noexcept = default;
	~intrusive_mpsc_stack () noexcept = default;

	intrusive_mpsc_stack (const intrusive_mpsc_stack &) = delete;
	intrusive_mpsc_stack &operator= (const intrusive_mpsc_stack &) = delete;

	// clang-format off

	/// Push \a node onto the top of the stack. Thread-safe; may be called
	/// concurrently from multiple producer threads. All writes to \a node
	/// before push() happen-before any reads from the returned node after
	/// try_pop(). Not wait-free: spins if another producer or the consumer is
	/// concurrently modifying the stack.
	void push (value_type &node) noexcept
	{
		auto *current = top_.load(std::memory_order_relaxed);
		(node.*Next).store(current, std::memory_order_relaxed);

		while (!top_.compare_exchange_weak(current, &node,
			std::memory_order_release,
			std::memory_order_relaxed))
		{
			(node.*Next).store(current, std::memory_order_relaxed);
		}
	}

	/// Remove and return the top node. Returns nullptr if empty or if a
	/// producer concurrently completed a push; the caller should retry.
	/// All writes to the returned node before its last push() happen-before
	/// reads after try_pop(). Consumer thread only.
	[[nodiscard]] value_type *try_pop () noexcept
	{
		auto *item = top_.load(std::memory_order_acquire);
		if (item == nullptr)
		{
			return nullptr;
		}

		auto *next = (item->*Next).load(std::memory_order_relaxed);
		if (top_.compare_exchange_strong(item, next,
			std::memory_order_relaxed,
			std::memory_order_relaxed))
		{
			return item;
		}

		return nullptr;
	}

	// clang-format on

	/// Return true if the stack has no elements. Consumer thread only.
	[[nodiscard]] bool empty () const noexcept
	{
		return top_.load(std::memory_order_relaxed) == nullptr;
	}

private:

	std::atomic<value_type *> top_{nullptr};
};

} // namespace pal
