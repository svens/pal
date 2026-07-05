#include <pal/require.hpp>
#include <cstdio>
#include <exception>

namespace pal::__require
{

[[noreturn]] void fail (const char *message) noexcept
{
	// NOLINTNEXTLINE(modernize-use-std-print)
	std::fprintf(stderr, "%s\n", message);
	std::terminate();
}

} // namespace pal::__require
