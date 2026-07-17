#pragma once

/**
 * \file pal/intrusive_queue.hpp
 * Intrusive queue (FIFO)
 */

#include <functional>
#include <iterator>
#include <utility>

namespace pal
{

/// Intrusive queue hook.
/// \see intrusive_queue
template <typename T>
using intrusive_queue_hook = T *;

/// \cond
template <auto Next>
class intrusive_queue;
/// \endcond

/// Intrusive queue (FIFO).
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
///   pal::intrusive_queue_hook<foo> next{};
///   using queue = pal::intrusive_queue<&foo::next>;
/// };
///
/// foo::queue q;
/// foo f;
/// q.push(f);
/// foo *fp = q.try_pop(); // fp == &f
/// \endcode
///
/// \note This container is not thread-safe.
template <typename T, typename Hook, Hook T::*Next>
class intrusive_queue<Next>
{
public:

	/// Element type of this container.
	using value_type = T;

	intrusive_queue () noexcept = default;
	~intrusive_queue () noexcept = default;

	intrusive_queue (const intrusive_queue &) = delete;
	intrusive_queue &operator= (const intrusive_queue &) = delete;

	/// Construct queue by moving contents of \a that. Leaves \a that empty.
	intrusive_queue (intrusive_queue &&that) noexcept
	{
		if (!that.empty())
		{
			head_ = std::exchange(that.head_, nullptr);
			tail_ = std::exchange(that.tail_, &that.head_);
		}
	}

	/// Move contents of \a that into this queue. Existing elements are dropped.
	/// Leaves \a that empty.
	intrusive_queue &operator= (intrusive_queue &&that) noexcept
	{
		if (!that.empty())
		{
			head_ = std::exchange(that.head_, nullptr);
			tail_ = std::exchange(that.tail_, &that.head_);
		}
		else
		{
			head_ = nullptr;
			tail_ = &head_;
		}
		return *this;
	}

	/// Push \a node onto the back of the queue.
	void push (value_type &node) noexcept
	{
		node.*Next = nullptr;
		*tail_ = &node;
		tail_ = &(node.*Next);
	}

	/// Remove and return the front node. Returns nullptr if empty.
	[[nodiscard]] value_type *try_pop () noexcept
	{
		if (auto it = head_)
		{
			head_ = it->*Next;
			if (head_ == nullptr)
			{
				tail_ = &head_;
			}
			return it;
		}
		return nullptr;
	}

	/// Remove and return the front node. Calling on an empty queue is undefined behaviour.
	[[nodiscard]] value_type *pop () noexcept
	{
		auto it = head_;
		head_ = it->*Next;
		if (head_ == nullptr)
		{
			tail_ = &head_;
		}
		return it;
	}

	/// Forward iterator over queue elements.
	struct iterator
	{
		using value_type = T;
		using difference_type = std::ptrdiff_t;
		using iterator_category = std::forward_iterator_tag;

		[[nodiscard]] value_type &operator* () const noexcept
		{
			return *node_;
		}

		[[nodiscard]] value_type *operator->() const noexcept
		{
			return node_;
		}

		iterator &operator++ () noexcept
		{
			node_ = node_->*Next;
			return *this;
		}

		iterator operator++ (int) noexcept
		{
			auto tmp = *this;
			++*this;
			return tmp;
		}

		[[nodiscard]] bool operator== (const iterator &) const noexcept = default;

		value_type *node_ = nullptr;
	};

	/// Return iterator to the front of the queue.
	[[nodiscard]] iterator begin () const noexcept
	{
		return {head_};
	}

	/// Return iterator past the back of the queue.
	[[nodiscard]] iterator end () const noexcept
	{
		return {};
	}

	/// Return the front node without removing it. Returns nullptr if empty.
	[[nodiscard]] value_type *head () const noexcept
	{
		return head_;
	}

	/// Return true if the queue has no elements.
	[[nodiscard]] bool empty () const noexcept
	{
		return head_ == nullptr;
	}

	/// Move all elements from \a that to the back of this queue. Leaves \a that empty.
	void splice (intrusive_queue &&that) noexcept
	{
		if (!that.empty())
		{
			*tail_ = that.head_;
			tail_ = std::exchange(that.tail_, &that.head_);
			that.head_ = nullptr;
		}
	}

	/// Remove \a node from the queue. Returns true if \a node was found and removed. O(n).
	bool remove (value_type &node) noexcept
	{
		for (value_type **it = &head_; *it; it = &((*it)->*Next))
		{
			if (*it == &node)
			{
				*it = node.*Next;
				if (*it == nullptr)
				{
					tail_ = it;
				}
				return true;
			}
		}
		return false;
	}

	/// Insert \a node before the first existing element for which \a cmp(node, element)
	/// is true. If no such element exists, \a node is appended to the back. O(n).
	template <typename Cmp = std::less<value_type>>
	void insert_sorted (value_type &node, Cmp cmp = Cmp{}) noexcept
	{
		value_type **it = &head_;
		while (*it && !cmp(node, **it))
		{
			it = &((*it)->*Next);
		}
		node.*Next = *it;
		*it = &node;
		if (!(node.*Next))
		{
			tail_ = &(node.*Next);
		}
	}

private:

	value_type *head_ = nullptr;
	value_type **tail_ = &head_;
};

} // namespace pal
