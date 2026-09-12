#pragma once

/**
 * \file pal/async/test.hpp
 * Test helpers for pal/async
 */

#include <pal/async/__event_loop.hpp>
#include <pal/version.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <string_view>
#include <utility>

namespace pal_test {

/// Backends the async suite runs against on this platform: a test case calling this runs once per value.
inline std::string_view test_backend ()
{
	if constexpr (pal::os == pal::os_type::linux)
	{
		return GENERATE("io_uring", "epoll");
	}
	else
	{
		return "default";
	}
}

/// Loop bound to \a backend, skipping the test case when this machine cannot provide it.
inline pal::result<pal::async::event_loop> make_test_loop (
	std::string_view backend,
	const pal::async::event_loop_config &config = {})
{
	CAPTURE(backend);

	if constexpr (pal::os == pal::os_type::linux)
	{
		using namespace pal::async::__event_loop;

		auto impl = (backend == "io_uring") ? make_io_uring_loop(config) : make_epoll_loop(config);
		if (!impl)
		{
			SKIP("backend unavailable: " << impl.error().message());
			return pal::unexpected{impl.error()};
		}
		return pal::async::event_loop{std::move(*impl)};
	}
	else
	{
		return pal::async::make_loop(config);
	}
}

} // namespace pal_test
