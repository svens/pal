configure_file(
	pal/version.cpp.in
	version.cpp
	@ONLY
)
list(APPEND pal_sources ${CMAKE_CURRENT_BINARY_DIR}/version.cpp)

list(APPEND pal_sources
	pal/error.hpp
	pal/error.cpp
	pal/result.hpp
	pal/version.hpp
)

list(APPEND pal_test_sources
	pal/test.hpp
	pal/test.cpp
	pal/error.test.cpp
	pal/result.test.cpp
)

list(APPEND pal_bench_sources
)
