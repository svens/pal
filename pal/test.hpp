#pragma once

#include <pal/codec.hpp>
#include <pal/version.hpp>
#include <catch2/interfaces/catch_interfaces_capture.hpp>
#include <string>

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

} // namespace pal_test
