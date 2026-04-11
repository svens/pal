#pragma once

/**
 * \file pal/intrusive_stack.hpp
 * Intrusive stack (LIFO)
 */

#include <utility>

namespace pal
{

/// Intrusive stack hook.
/// \see intrusive_stack
template <typename T>
using intrusive_stack_hook = T *;

/// \cond
template <auto Next>
class intrusive_stack;
/// \endcond

/// Intrusive stack (LIFO).
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
///   pal::intrusive_stack_hook<foo> next{};
///   using stack = pal::intrusive_stack<&foo::next>;
/// };
///
/// foo::stack s;
/// foo f;
/// s.push(f);
/// foo *fp = s.try_pop(); // fp == &f
/// \endcode
///
/// \note This container is not thread-safe.
template <typename T, typename Hook, Hook T::*Next>
class intrusive_stack<Next>
{
public:

	/// Element type of this container.
	using value_type = T;

	intrusive_stack () noexcept = default;
	~intrusive_stack () noexcept = default;

	intrusive_stack (const intrusive_stack &) = delete;
	intrusive_stack &operator= (const intrusive_stack &) = delete;

	/// Construct stack by moving contents of \a that. Leaves \a that empty.
	intrusive_stack (intrusive_stack &&that) noexcept
		: top_(std::exchange(that.top_, nullptr))
	{
	}

	/// Move contents of \a that into this stack. Leaves \a that empty.
	intrusive_stack &operator= (intrusive_stack &&that) noexcept
	{
		top_ = std::exchange(that.top_, nullptr);
		return *this;
	}

	/// Push \a node onto the top of the stack.
	void push (value_type &node) noexcept
	{
		node.*Next = top_;
		top_ = &node;
	}

	/// Remove and return the top node. Returns nullptr if empty.
	[[nodiscard]] value_type *try_pop () noexcept
	{
		auto node = top_;
		if (node)
		{
			top_ = top_->*Next;
		}
		return node;
	}

	/// Remove and return the top node. Calling on an empty stack is undefined behaviour.
	[[nodiscard]] value_type *pop () noexcept
	{
		return std::exchange(top_, top_->*Next);
	}

	/// Return the top node without removing it. Returns nullptr if empty.
	[[nodiscard]] value_type *top () const noexcept
	{
		return top_;
	}

	/// Return true if the stack has no elements.
	[[nodiscard]] bool empty () const noexcept
	{
		return top_ == nullptr;
	}

private:

	value_type *top_ = nullptr;
};

} // namespace pal
