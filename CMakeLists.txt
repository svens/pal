cmake_minimum_required(VERSION 3.28)

project(pal LANGUAGES CXX)
include(cmake/project_version.cmake)
message(STATUS "${PROJECT_NAME} ${PROJECT_VERSION}")

include(cmake/cxx.cmake)

include(pal/list.cmake)
add_library(pal ${pal_sources})
add_library(pal::pal ALIAS pal)

target_include_directories(pal PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
target_link_libraries(pal PRIVATE cxx_warnings)

if(PROJECT_IS_TOP_LEVEL)
	include(cmake/test.cmake)

	if(pal_test_sources)
		add_executable(pal_test ${pal_test_sources})
		target_link_libraries(pal_test PRIVATE cxx_test pal::pal)
		catch_discover_tests(pal_test
			EXTRA_ARGS --skip-benchmarks
		)
	endif()

	if(pal_bench_sources)
		add_executable(pal_bench ${pal_bench_sources})
		target_link_libraries(pal_bench PRIVATE cxx_bench pal::pal)
	endif()
endif()
