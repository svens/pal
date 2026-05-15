#pragma once

/**
 * \file pal/codec.hpp
 * Encoding/decoding codecs (base64, hex)
 */

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <system_error>
#include <utility>

namespace pal
{

// clang-format off

namespace __codec
{

template <typename T>
concept byte_type =
	std::same_as<std::remove_cv_t<T>, char> ||
	std::same_as<std::remove_cv_t<T>, unsigned char> ||
	std::same_as<std::remove_cv_t<T>, std::byte> ||
	std::same_as<std::remove_cv_t<T>, uint8_t>
;

template <typename It>
concept byte_pointer =
	std::is_pointer_v<It> &&
	!std::is_const_v<std::remove_pointer_t<It>> &&
	byte_type<std::remove_pointer_t<It>>
;

template <typename It>
concept const_byte_pointer =
	std::is_pointer_v<It> &&
	byte_type<std::remove_pointer_t<It>>
;

} // namespace __codec

/// Result type returned by convert().
/// Mirrors std::to_chars_result: on success \c ptr points past the last written element
/// and \c ec is zero; on failure \c ec carries the error and \c ptr equals \c last.
template <typename It>
struct convert_result
{
	It ptr;
	std::errc ec;
};

/// Requirements for types used as codec arguments.
/// Codecs may be stateless (static methods on empty structs) or stateful
/// (instance methods carrying configuration or streaming context).
template <typename C>
concept codec = requires(std::remove_cvref_t<C> &c,
	size_t n,
	const std::byte *first,
	const std::byte *last,
	std::byte *out,
	std::byte *out_last)
{
	{ c.max_size(n) } noexcept -> std::same_as<size_t>;
	{ c.convert(first, last, out, out_last) } noexcept -> std::same_as<convert_result<std::byte *>>;
};

// clang-format on

/// Returns maximum output storage required to convert \a input using \a c.
/// This is an upper bound; the convert_result::ptr returned by convert() carries the true length.
template <codec C, std::ranges::contiguous_range Input>
	requires __codec::byte_type<std::ranges::range_value_t<Input>>
constexpr size_t convert_max_size (C &&c, const Input &input) noexcept
{
	return c.max_size(std::ranges::size(input));
}

/// Returns maximum output storage required to convert [\a first, \a last) using \a c.
template <codec C, __codec::const_byte_pointer InIt>
constexpr size_t convert_max_size (C &&c, InIt first, InIt last) noexcept
{
	return c.max_size(static_cast<size_t>(last - first));
}

/// Convert [\a in_first, \a in_last) using \a c, writing output to [\a first, \a last).
/// On success returns convert_result with ptr past last written element and ec zero.
/// On failure returns convert_result with ptr equal to \a last and ec carrying the error.
template <codec C, __codec::byte_pointer OutIt, __codec::const_byte_pointer InIt>
convert_result<OutIt> convert (C &&c, OutIt first, OutIt last, InIt in_first, InIt in_last) noexcept
{
	auto *out = reinterpret_cast<std::byte *>(first);
	auto *out_end = reinterpret_cast<std::byte *>(last);
	const auto *in = reinterpret_cast<const std::byte *>(in_first);
	const auto *in_end = reinterpret_cast<const std::byte *>(in_last);
	auto [end, ec] = c.convert(in, in_end, out, out_end);
	if (ec == std::errc{})
	{
		return {.ptr = first + (end - out), .ec = {}};
	}
	return {.ptr = last, .ec = ec};
}

/// Convert \a input using \a c into \a output.
/// Convenience overload; delegates to the pointer-pair form.
template <codec C, std::ranges::contiguous_range Output, std::ranges::contiguous_range Input>
	requires __codec::byte_pointer<decltype(std::ranges::data(std::declval<Output &>()))>
	&& __codec::byte_type<std::ranges::range_value_t<Input>>
auto convert (C &&c, Output &output, const Input &input) noexcept
{
	auto out = std::ranges::data(output);
	auto out_end = std::ranges::data(output) + std::ranges::size(output);
	auto in = std::ranges::data(input);
	auto in_end = std::ranges::data(input) + std::ranges::size(input);
	return convert(std::forward<C>(c), out, out_end, in, in_end);
}

// base64 {{{1

/// Base64 encoding codec type
struct base64_encoder
{
	static constexpr size_t max_size (size_t n) noexcept
	{
		return ((n * 4 / 3) + 3) & ~3U;
	}

	static convert_result<std::byte *>
	convert (const std::byte *first, const std::byte *last, std::byte *out, std::byte *out_last) noexcept;
};
inline constexpr base64_encoder base64_encode{};

/// Base64 decoding codec type
struct base64_decoder
{
	static constexpr size_t max_size (size_t n) noexcept
	{
		return n / 4 * 3;
	}

	static convert_result<std::byte *>
	convert (const std::byte *first, const std::byte *last, std::byte *out, std::byte *out_last) noexcept;
};
inline constexpr base64_decoder base64_decode{};

// hex {{{1

/// Hex-string encoding codec type. Produces lowercase output.
struct hex_encoder
{
	static constexpr size_t max_size (size_t n) noexcept
	{
		return n * 2;
	}

	static convert_result<std::byte *>
	convert (const std::byte *first, const std::byte *last, std::byte *out, std::byte *out_last) noexcept;
};
inline constexpr hex_encoder hex_encode{};

/// Hex-string decoding codec type. Accepts both upper- and lowercase.
struct hex_decoder
{
	static constexpr size_t max_size (size_t n) noexcept
	{
		return n / 2;
	}

	static convert_result<std::byte *>
	convert (const std::byte *first, const std::byte *last, std::byte *out, std::byte *out_last) noexcept;
};
inline constexpr hex_decoder hex_decode{};

// }}}1

} // namespace pal
