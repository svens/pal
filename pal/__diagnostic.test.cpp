#include <pal/__diagnostic>

namespace {

// Build-time test: expect compilation succeed silently
//
// And yes, function below should be static in anonymous namespace for MSVC to
// trigger warning when not silenced

__pal_diagnostic(push)
__pal_diagnostic_suppress(__pal_unused_parameter)
__pal_diagnostic_suppress(__pal_unused_function)

static void foo (int argc, const char *argv[])
{
}

__pal_diagnostic(pop)

} // namespace
