#if !defined(_CRT_SECURE_NO_WARNINGS)
	#define _CRT_SECURE_NO_WARNINGS
#endif
#include <cstdlib>

#include <pal/test>
#include <catch2/catch_session.hpp>

int main (int argc, char *argv[])
{
	return Catch::Session().run(argc, argv);
}

bool pal_test::__on_ci ()
{
	static const bool has_env = (std::getenv("CI") != nullptr);
	return has_env;
}
