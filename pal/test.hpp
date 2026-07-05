#pragma once

#include <pal/codec.hpp>
#include <pal/version.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/interfaces/catch_interfaces_capture.hpp>
#include <string>
#include <type_traits>
#include <utility>

namespace pal_test
{

/// Convert a range of bytes to a lowercase hex string.
template <typename T>
std::string to_hex (const T &data)
{
	std::string result(pal::convert_max_size(pal::hex_encode, data), '\0');
	std::ignore = pal::convert(pal::hex_encode, result, data);
	return result;
}

inline std::string case_name ()
{
	return Catch::getResultCapture().getCurrentTestName();
}

bool on_ci_impl () noexcept;
inline const bool on_ci = on_ci_impl();

struct bad_alloc_once
{
	static inline bool fail = false;

	bad_alloc_once () noexcept
	{
		fail = true;
	}

	~bad_alloc_once () noexcept
	{
		fail = false;
	}
};

/// Generic helper for two-sided tests
template <typename Server, typename Client = Server>
struct connected_pair
{
	Server server;
	Client client;
};

/// \internal Implementation for require_terminate(); lives in test.cpp so
/// the setjmp/longjmp and stderr-redirection details stay out of this
/// header. Invokes \a fn(context) with std::terminate() intercepted and
/// stderr redirected to a capture buffer for the duration of the call;
/// returns {trapped, captured stderr text}.
std::pair<bool, std::string> __require_terminate (void (*fn)(void *context), void *context);

/// Run \a fn (typically a lambda wrapping a failing pal_require) expecting
/// it to invoke std::terminate(); REQUIREs that it did, trapping the
/// termination instead of aborting the test binary. Returns anything \a fn
/// wrote to stderr while running (captured instead of polluting Catch2's own
/// output), for the caller to inspect if needed.
///
/// \a fn and the scope it runs in must be free of non-trivial destructors:
/// on trap, control returns via longjmp, which does not run C++
/// destructors.
template <typename F>
std::string require_terminate (F &&fn)
{
	auto [trapped, stderr_text] = __require_terminate(
		[] (void *context) { (*static_cast<std::remove_reference_t<F> *>(context))(); }, &fn
	);
	REQUIRE(trapped);
	return stderr_text;
}

} // namespace pal_test
