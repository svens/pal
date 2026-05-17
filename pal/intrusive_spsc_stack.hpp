#pragma once

/**
 * \file pal/intrusive_spsc_stack.hpp
 * Intrusive lock-free SPSC stack (LIFO)
 */

#include <atomic>

namespace pal
{

/// Intrusive SPSC stack hook.
/// \see intrusive_spsc_stack
template <typename T>
using intrusive_spsc_stack_hook = std::atomic<T *>;

/// \cond
template <auto Next>
class intrusive_spsc_stack;
/// \endcond

/// Intrusive single-producer single-consumer lock-free stack (SPSC LIFO).
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
///   pal::intrusive_spsc_stack_hook<foo> hook{};
///   using stack = pal::intrusive_spsc_stack<&foo::hook>;
/// };
///
/// foo::stack s;
/// foo f;
/// s.push(f);
/// foo *fp = s.try_pop(); // fp == &f
/// \endcode
///
/// \note push() must be called from a single producer thread only.
/// try_pop() and empty() must only be called from the consumer thread.
/// try_pop() returns nullptr if empty or transiently inconsistent
/// (producer is mid-push); the caller should retry.
///
template <typename T, typename Hook, Hook T::*Next>
class intrusive_spsc_stack<Next>
{
public:

	/// Element type of this container.
	using value_type = T;

	intrusive_spsc_stack () noexcept = default;
	~intrusive_spsc_stack () noexcept = default;

	intrusive_spsc_stack (const intrusive_spsc_stack &) = delete;
	intrusive_spsc_stack &operator= (const intrusive_spsc_stack &) = delete;

	// clang-format off

	/// Push \a node onto the top of the stack. Single producer thread only.
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

	/// Remove and return the top node. Returns nullptr if empty or transiently
	/// inconsistent (producer is mid-push). Consumer thread only.
	[[nodiscard]] value_type *try_pop () noexcept
	{
		auto *item = top_.load(std::memory_order_acquire);
		if (item == nullptr)
		{
			return nullptr;
		}

		auto *next = (item->*Next).load(std::memory_order_relaxed);
		if (top_.compare_exchange_strong(item, next,
			std::memory_order_release,
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
		return top_.load(std::memory_order_acquire) == nullptr;
	}

private:

	std::atomic<value_type *> top_{nullptr};
};

} // namespace pal
