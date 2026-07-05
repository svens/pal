#pragma once

/**
 * \file pal/require.hpp
 * Precondition check for contract violations
 */

#include <pal/version.hpp>
#include <type_traits>
#include <utility>

// NOLINTNEXTLINE(modernize-concat-nested-namespaces)
namespace pal
{

/// Require \a Expr to evaluate to true. \a Expr is always evaluated; on success,
/// its value is returned unchanged, so \c pal_require can be used inline.
///
/// Enforcement is compiled in for debug builds only (\c pal::build). On failure,
/// prints the \a OptionalMessage if given, otherwise "file:line: Unexpected 'Expr'",
/// then calls \c std::terminate().
#define pal_require(Expr, OptionalMessage) internal

#if !defined(__DOXYGEN__) //{{{

namespace __require
{

template <typename T, typename = void>
constexpr bool is_pointer_like_v = false;

template <typename T>
constexpr bool is_pointer_like_v<T, std::void_t<decltype(&T::operator->), decltype(&T::operator*)>> = true;

template <typename T>
constexpr bool is_pointer_v = std::is_pointer_v<T> || is_pointer_like_v<T>;

[[noreturn]] void fail (const char *message) noexcept;

template <typename T>
constexpr decltype(auto) check (T &&expr, const char *message, const char * = nullptr) noexcept
{
	if constexpr (build == build_type::debug)
	{
		if (!expr)
		{
			fail(message);
		}
	}
	return std::forward<T>(expr);
}

} // namespace __require

#define __pal_str_impl(S) #S
#define __pal_str(S) __pal_str_impl(S)
#define __pal_at __FILE__ ":" __pal_str(__LINE__)

#undef pal_require
#define pal_require(...) \
	__pal_require(__VA_ARGS__, __pal_require_msg(__VA_ARGS__, _), _)

#define __pal_require(Expr, Message, ...) ::pal::__require::check(Expr, Message)

#define __pal_require_msg(Expr, ...) \
	(::pal::__require::is_pointer_v<decltype(Expr)> \
			? __pal_at ": Unexpected '" #Expr " == nullptr'" \
			: __pal_at ": Unexpected '" #Expr "'")

#endif // __DOXYGEN__ }}}

} // namespace pal
