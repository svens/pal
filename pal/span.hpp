#pragma once

/**
 * \file pal/span.hpp
 * Utilities for treating std::span and sequences of spans uniformly
 */

#include <iterator>
#include <span>

namespace pal
{

namespace __span
{

template <typename T>
constexpr bool is_span_v = false;

template <typename T, size_t Extent>
constexpr bool is_span_v<std::span<T, Extent>> = true;

} // namespace __span

/// Treat single span as one-element sequence: return pointer to \a span.
template <typename T, size_t Extent>
constexpr const std::span<T, Extent> *span_sequence_begin (const std::span<T, Extent> &span) noexcept
{
	return std::addressof(span);
}

/// Treat single span as one-element sequence: return pointer past \a span.
template <typename T, size_t Extent>
constexpr const std::span<T, Extent> *span_sequence_end (const std::span<T, Extent> &span) noexcept
{
	return std::addressof(span) + 1;
}

/// Return iterator to first span in \a spans.
template <typename SpanSequence>
	requires(!__span::is_span_v<SpanSequence>)
constexpr auto span_sequence_begin (SpanSequence &spans) noexcept -> decltype(std::begin(spans))
{
	return std::begin(spans);
}

/// Return iterator past last span in \a spans.
template <typename SpanSequence>
	requires(!__span::is_span_v<SpanSequence>)
constexpr auto span_sequence_end (SpanSequence &spans) noexcept -> decltype(std::end(spans))
{
	return std::end(spans);
}

/// Return iterator to first span in const \a spans.
template <typename SpanSequence>
	requires(!__span::is_span_v<SpanSequence>)
constexpr auto span_sequence_begin (const SpanSequence &spans) noexcept
	-> decltype(std::begin(spans))
{
	return std::begin(spans);
}

/// Return iterator past last span in const \a spans.
template <typename SpanSequence>
	requires(!__span::is_span_v<SpanSequence>)
constexpr auto span_sequence_end (const SpanSequence &spans) noexcept -> decltype(std::end(spans))
{
	return std::end(spans);
}

/// Return total size in bytes of all spans in \a spans.
template <typename SpanSequence>
constexpr size_t span_size_bytes (const SpanSequence &spans) noexcept
{
	size_t result = 0;
	const auto end = span_sequence_end(spans);
	for (auto it = span_sequence_begin(spans); it != end; ++it)
	{
		result += it->size_bytes();
	}
	return result;
}

} // namespace pal
