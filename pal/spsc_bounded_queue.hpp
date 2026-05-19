#pragma once

/**
 * \file pal/spsc_bounded_queue.hpp
 * Bounded lock-free SPSC queue (FIFO)
 */

#include <pal/__diagnostic.hpp>
#include <atomic>
#include <cstddef>

namespace pal
{

/// Bounded single-producer single-consumer lock-free queue (SPSC FIFO).
///
/// Stores up to \a N pointers to externally-owned elements. No hook is
/// required in the element type.
///
/// Usage:
/// \code
/// struct task { /* ... */ };
///
/// pal::spsc_bounded_queue<task, 64> results;
/// task t;
/// results.push(t);
/// task *p = results.try_pop(); // p == &t
/// \endcode
///
/// \note push() must be called from a single producer thread only.
/// try_pop(), empty(), and full() must only be called from their respective
/// thread: empty() from the consumer, full() from the producer.
/// All writes to an element before push() happen-before any reads after
/// try_pop() returns it.
///
/// \note N need not be a power of two; with compile-time N the compiler
/// performs strength reduction on the modulo regardless. A power-of-two
/// capacity produces a single AND instruction instead of a
/// multiply-by-reciprocal sequence — negligible for typical use but worth
/// preferring when the choice is free.
///
template <typename T, size_t N>
class spsc_bounded_queue
{
public:

	static_assert(N > 0);

	/// Element type of this container.
	using value_type = T;

	spsc_bounded_queue () noexcept = default;
	~spsc_bounded_queue () noexcept = default;

	spsc_bounded_queue (const spsc_bounded_queue &) = delete;
	spsc_bounded_queue &operator= (const spsc_bounded_queue &) = delete;

	/// Push \a item. Returns false if full. Producer thread only.
	bool push (value_type &item) noexcept
	{
		const auto tail = tail_.load(std::memory_order_relaxed);
		if (tail - head_.load(std::memory_order_acquire) != N)
		{
			slots_[tail % N] = &item;
			tail_.store(tail + 1, std::memory_order_release);
			return true;
		}
		return false;
	}

	/// Remove and return the front element. Returns nullptr if empty.
	/// Consumer thread only.
	[[nodiscard]] value_type *try_pop () noexcept
	{
		const auto head = head_.load(std::memory_order_relaxed);
		if (head != tail_.load(std::memory_order_acquire))
		{
			auto *item = slots_[head % N];
			head_.store(head + 1, std::memory_order_release);
			return item;
		}
		return nullptr;
	}

	/// Return true if the queue has no elements. Consumer thread only.
	[[nodiscard]] bool empty () const noexcept
	{
		return head_.load(std::memory_order_relaxed) == tail_.load(std::memory_order_acquire);
	}

	/// Return true if no slots are available. Producer thread only.
	[[nodiscard]] bool full () const noexcept
	{
		return tail_.load(std::memory_order_relaxed) - head_.load(std::memory_order_acquire) == N;
	}

private:

	value_type *slots_[N]{};

	// clang-format off
	__pal_diagnostic(push)
	__pal_diagnostic_suppress(__pal_aligned_struct_padding)

	alignas(cache_line_size) std::atomic<size_t> tail_;
	alignas(cache_line_size) std::atomic<size_t> head_;

	__pal_diagnostic(pop)
	// clang-format on
};

} // namespace pal
