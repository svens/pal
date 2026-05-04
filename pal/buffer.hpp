#pragma once

/**
 * \file pal/buffer.hpp
 * Buffer concepts and helpers for I/O operations
 */

#include <array>
#include <concepts>
#include <cstddef>
#include <ranges>
#include <span>

namespace pal
{

// clang-format off
// Requires expression braces: Allman style not applied (see .clang-format TODO #7)

/// Requirements for types used as read-only I/O buffer
template <typename T>
concept const_buffer = requires(const T &b)
{
	{ std::data(b) } -> std::convertible_to<const void *>;
	{ std::size(b) } -> std::same_as<size_t>;
	requires std::is_trivially_copyable_v<std::remove_cvref_t<decltype(*std::data(b))>>;
};

/// Requirements for types used as writable I/O buffer
template <typename T>
concept mutable_buffer = requires(T &b)
{
	{ std::data(b) } -> std::convertible_to<void *>;
	{ std::size(b) } -> std::same_as<size_t>;
	requires std::is_trivially_copyable_v<std::remove_cvref_t<decltype(*std::data(b))>>;
};

/// Requirements for sequences of read-only I/O buffers (scatter/gather send)
template <typename T>
concept const_buffer_sequence =
	const_buffer<std::ranges::range_value_t<T>> &&
	std::ranges::input_range<T>
;

/// Requirements for sequences of writable I/O buffers (scatter/gather receive)
template <typename T>
concept mutable_buffer_sequence =
	mutable_buffer<std::ranges::range_value_t<T>> &&
	std::ranges::input_range<T>
;

// clang-format on

namespace __buffer
{

/// True when T is plain data safe to use as raw I/O bytes:
/// trivially copyable and not a view/container that provides its own buffer interface
template <typename T>
constexpr bool is_byte_safe_v = std::is_trivially_copyable_v<T> && !const_buffer<T>;

/// True when R's elements are writable (not inherently const)
template <typename R>
constexpr bool is_writable_range_v = !std::is_const_v<std::remove_reference_t<std::ranges::range_reference_t<R>>>;

} // namespace __buffer

/// Create read-only byte buffer view of a single value \a t
template <typename T>
	requires __buffer::is_byte_safe_v<T>
auto buffer (const T *t) noexcept -> std::array<std::span<const std::byte>, 1>
{
	return {std::as_bytes(std::span{t, 1})};
}

/// Create writable byte buffer view of a single value \a t
template <typename T>
	requires __buffer::is_byte_safe_v<T>
auto buffer (T *t) noexcept -> std::array<std::span<std::byte>, 1>
{
	return {std::as_writable_bytes(std::span{t, 1})};
}

/// Create read-only byte buffer view(s) from one or more contiguous ranges \a rs
template <std::ranges::contiguous_range... Rs>
	requires(__buffer::is_byte_safe_v<std::ranges::range_value_t<Rs>> && ...)
auto buffer (const Rs &...rs) noexcept -> std::array<std::span<const std::byte>, sizeof...(Rs)>
{
	return {std::as_bytes(std::span{rs})...};
}

/// Create writable byte buffer view(s) from one or more contiguous ranges \a rs
template <std::ranges::contiguous_range... Rs>
	requires(__buffer::is_byte_safe_v<std::ranges::range_value_t<Rs>> && ...)
	&& (__buffer::is_writable_range_v<Rs> && ...)
auto buffer (Rs &...rs) noexcept -> std::array<std::span<std::byte>, sizeof...(Rs)>
{
	return {std::as_writable_bytes(std::span{rs})...};
}

} // namespace pal
