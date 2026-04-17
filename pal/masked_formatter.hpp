#pragma once

/**
 * \file pal/masked_formatter.hpp
 * Privacy-safe formatting wrapper
 */

#include <format>

namespace pal
{

/// Wraps a const reference to \a value for privacy-safe (masked) formatting
template <typename T>
struct masked
{
	const T &value;
};

template <typename T>
masked(const T &) -> masked<T>;

/// Customization point for masked formatting. Specialize for each type \a T.
/// Required member:
///   template <typename FormatContext>
///   static FormatContext::iterator format(const T &, FormatContext &);
template <typename T>
struct masked_formatter;

} // namespace pal

namespace std
{

template <typename T>
struct formatter<pal::masked<T>>
{
	template <typename ParseContext>
	static constexpr ParseContext::iterator parse (ParseContext &ctx)
	{
		return ctx.begin();
	}

	template <typename FormatContext>
	FormatContext::iterator format (const pal::masked<T> &m, FormatContext &ctx) const
	{
		return pal::masked_formatter<T>::format(m.value, ctx);
	}
};

} // namespace std
