#pragma once

/**
 * \file pal/uri.hpp
 * RFC 3986 URI parser and generator
 */

#include <pal/net/ip/address.hpp>
#include <pal/net/ip/port_type.hpp>
#include <pal/result.hpp>
#include <charconv>
#include <cstddef>
#include <optional>
#include <string_view>
#include <variant>

namespace pal
{

/**
 * URI host (RFC 3986 section 3.2.2).
 *
 * Holds either reg-name text (still percent-encoded, possibly empty) or the IP address of an IPv4address or IP-literal.
 * IPvFuture and IPv6 zone identifiers are not supported.
 */
struct uri_host
{
	/// Reg-name text or IP address
	std::variant<std::string_view, net::ip::address> value{};

	/// Return host parsed from all of \a text.
	/// On failure returns std::errc::invalid_argument.
	[[nodiscard]] static result<uri_host> make (std::string_view text) noexcept;

	/**
	 * Parse host from the start of [\a first, \a last).
	 *
	 * On success returns std::from_chars_result with ptr past the closing \c ']' of an IP-literal or at the first
	 * character that can not continue a reg-name, and ec zero. On failure (including a \c '%' not followed by two
	 * hex digits) ec is std::errc::invalid_argument; ptr and \a this are unspecified.
	 */
	[[nodiscard]] std::from_chars_result from_chars (const char *first, const char *last) noexcept;

	/**
	 * Write host into [\a first, \a last): reg-name as is, IPv4 address in dotted form, IPv6 address inside
	 * brackets.
	 *
	 * On success returns std::to_chars_result with ptr past last written character and ec zero. On failure ptr is
	 * \a last and ec is:
	 * - std::errc::invalid_argument if !is_valid()
	 * - std::errc::value_too_large if output does not fit
	 */
	[[nodiscard]] std::to_chars_result to_chars (char *first, char *last) const noexcept;

	/// Return true if reg-name text is valid reg-name that does not parse as IPv4address, or IPv6 address has zero
	/// scope_id.
	[[nodiscard]] bool is_valid () const noexcept;

	/// Return maximum number of characters to_chars() writes.
	[[nodiscard]] size_t max_size () const noexcept;

	friend constexpr bool operator== (const uri_host &, const uri_host &) noexcept = default;
};

/**
 * URI-reference (RFC 3986 section 4.1).
 *
 * Text components are still percent-encoded (see pal::percent_encoder). An
 * absent component is distinct from an empty one (\c "a:?" has an empty
 * query, \c "a:" has none).
 */
struct uri_view
{
	/// Scheme, absent for relative-ref
	std::optional<std::string_view> scheme{};

	/// Userinfo, without the trailing \c '@'; requires \a host
	std::optional<std::string_view> userinfo{};

	/// Host text, IP-literal including brackets (see uri_host::make());
	/// present exactly when the reference has an authority
	std::optional<std::string_view> host{};

	/// Port, if present and non-empty; requires \a host
	std::optional<net::ip::port_type> port{};

	/// Path, possibly empty
	std::string_view path{};

	/// Query, without the leading \c '?'
	std::optional<std::string_view> query{};

	/// Fragment, without the leading \c '#'
	std::optional<std::string_view> fragment{};

	/// Return URI-reference parsed from all of \a text. Components are views into \a text.
	/// On failure returns std::errc::invalid_argument.
	[[nodiscard]] static result<uri_view> make (std::string_view text) noexcept;

	/**
	 * Parse URI-reference from all of [\a first, \a last). Components are views into the input.
	 *
	 * On success returns std::from_chars_result with ptr \a last and ec zero. On failure ec is
	 * std::errc::invalid_argument; ptr and \a this are unspecified.
	 */
	[[nodiscard]] std::from_chars_result from_chars (const char *first, const char *last) noexcept;

	/**
	 * Write URI-reference into [\a first, \a last), validating the reference as make() does and inserting
	 * delimiters. Besides each component's own grammar this requires:
	 * - with a host, \a path empty or starting with \c '/'
	 * - without a host, \a path not starting with \c "//"
	 * - without scheme and host, no \c ':' in the first \a path segment
	 *
	 * On success returns std::to_chars_result with ptr past last written character and ec zero. On failure ptr is
	 * \a last and ec is:
	 * - std::errc::invalid_argument for invalid components
	 * - std::errc::value_too_large if output does not fit
	 */
	[[nodiscard]] std::to_chars_result to_chars (char *first, char *last) const noexcept;

	/// Return maximum number of characters to_chars() writes.
	[[nodiscard]] size_t max_size () const noexcept;

	friend constexpr bool operator== (const uri_view &, const uri_view &) noexcept = default;
};

} // namespace pal
