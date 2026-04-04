#pragma once

#include <pal/version.hpp>
#include <catch2/interfaces/catch_interfaces_capture.hpp>
#include <string>

namespace pal_test
{

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

} // namespace pal_test
