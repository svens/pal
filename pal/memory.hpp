#pragma once

/**
 * \file pal/memory.hpp
 * Additional helper utilities for STL <memory> header
 */

#include <pal/result.hpp>
#include <array>
#include <memory>
#include <new>
#include <span>

namespace pal
{

/// Temporary scratch buffer: up to \a StackSize bytes are allocated inline.
/// If the runtime \a size exceeds \a StackSize, memory is allocated from the
/// heap and freed on destruction.
///
/// Non-copyable and non-movable: the heap pointer aliases the inline storage,
/// so the object cannot be relocated.
template <size_t StackSize>
class temporary_buffer
{
public:

	static_assert(StackSize > 0);

	temporary_buffer () = delete;
	temporary_buffer (const temporary_buffer &) = delete;
	temporary_buffer &operator= (const temporary_buffer &) = delete;
	temporary_buffer (temporary_buffer &&) = delete;
	temporary_buffer &operator= (temporary_buffer &&) = delete;

	/// Allocate \a size bytes. On heap allocation failure, operator bool
	/// returns false and get() returns an empty span.
	temporary_buffer (std::nothrow_t, size_t size) noexcept
	{
		if (size > StackSize)
		{
			if (auto *p = ::operator new (size, std::nothrow))
			{
				span_ = {static_cast<std::byte *>(p), size};
			}
		}
		else
		{
			span_ = {stack_.data(), size};
		}
	}

	/// Allocate \a size bytes. On heap allocation failure, throws std::bad_alloc.
	explicit temporary_buffer (size_t size)
	{
		if (size > StackSize)
		{
			span_ = {static_cast<std::byte *>(::operator new (size)), size};
		}
		else
		{
			span_ = {stack_.data(), size};
		}
	}

	~temporary_buffer () noexcept
	{
		if (span_.data() != stack_.data())
		{
			::operator delete (span_.data());
		}
	}

	/// Returns a span over the allocated buffer, or an empty span on nothrow failure.
	[[nodiscard]] std::span<std::byte> get () noexcept
	{
		return span_;
	}

	/// Returns true if the buffer is using inline storage (not heap-allocated).
	[[nodiscard]] bool is_inline () const noexcept
	{
		return span_.data() == stack_.data();
	}

	/// Returns true if the allocation succeeded.
	explicit operator bool () const noexcept
	{
		return span_.data() != nullptr;
	}

private:

	std::array<std::byte, StackSize> stack_;
	std::span<std::byte> span_;
};

/// Wrap \a alloc(\a args...) in a pal::result, converting std::bad_alloc and
/// nullptr returns into unexpected{std::errc::not_enough_memory}.
template <typename... Args>
auto alloc_result (auto &&alloc, Args &&...args) -> result<decltype(alloc(std::forward<Args>(args)...))>
{
	using R = decltype(alloc(std::forward<Args>(args)...));
	try
	{
		if constexpr (std::is_void_v<R>)
		{
			alloc(std::forward<Args>(args)...);
			return {};
		}
		else
		{
			if (auto p = alloc(std::forward<Args>(args)...))
			{
				return std::move(p);
			}
			return make_unexpected(std::errc::not_enough_memory);
		}
	}
	catch (const std::bad_alloc &)
	{
		return make_unexpected(std::errc::not_enough_memory);
	}
}

/// Wrapper for std::make_unique<T>() returning pal::result instead of throwing.
template <typename T, typename... Args>
result<std::unique_ptr<T>> make_unique (Args &&...args)
{
	return alloc_result([&] { return std::make_unique<T>(std::forward<Args>(args)...); });
}

/// Wrapper for std::make_shared<T>() returning pal::result instead of throwing.
template <typename T, typename... Args>
result<std::shared_ptr<T>> make_shared (Args &&...args)
{
	return alloc_result([&] { return std::make_shared<T>(std::forward<Args>(args)...); });
}

} // namespace pal
