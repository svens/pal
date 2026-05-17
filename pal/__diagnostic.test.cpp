#include <pal/__diagnostic.hpp>

namespace
{

// Compile-time test: without suppression these trigger -Wunused-function /
// -Wunused-parameter (GCC/Clang) or C4505 / C4100 (MSVC)

// clang-format off

__pal_diagnostic(push)
__pal_diagnostic_suppress(__pal_unused_function)
__pal_diagnostic_suppress(__pal_unused_parameter)

// NOLINTNEXTLINE(readability-static-definition-in-anonymous-namespace)
static void unused (int argc, const char *argv[])
{
}

__pal_diagnostic(pop)

// clang-format on

} // namespace
