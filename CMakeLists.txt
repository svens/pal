cmake_minimum_required(VERSION 3.28)

project(pal LANGUAGES CXX)
include(cmake/project_version.cmake)
message(STATUS "${PROJECT_NAME} ${PROJECT_VERSION}")

option(PAL_FUZZ "Build fuzz targets" OFF)

include(cmake/cxx.cmake)
include(cmake/system_libs.cmake)

include(pal/list.cmake)
pal_system_libs_add(
	WINDOWS ws2_32
)
configure_file(
	pal/version.cpp.in
	version.cpp
	@ONLY
)
list(APPEND pal_sources
	${CMAKE_CURRENT_BINARY_DIR}/version.cpp
)
add_library(pal ${pal_sources})
add_library(pal::pal ALIAS pal)

target_include_directories(pal PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
target_link_libraries(pal PRIVATE cxx_warnings ${pal_system_libs})

if(PROJECT_IS_TOP_LEVEL)
	include(cmake/tidy.cmake)
	include(cmake/test.cmake)

	add_executable(pal_test ${pal_test_sources})
	target_link_libraries(pal_test PRIVATE cxx_test pal::pal)
	add_test(NAME pal_test COMMAND pal_test --skip-benchmarks)

	if(PAL_FUZZ)
		foreach(source IN LISTS pal_fuzz_sources)
			if(NOT source MATCHES "\\.fuzz\\.cpp$")
				continue()
			endif()
			string(REGEX REPLACE "\\.fuzz\\.cpp$" "" fuzz_path "${source}")
			string(REPLACE "/" "-" fuzz_exe "${fuzz_path}")
			add_executable(fuzz-${fuzz_exe} ${source})
			target_link_libraries(fuzz-${fuzz_exe} PRIVATE pal::pal)
			target_link_options(fuzz-${fuzz_exe} PRIVATE -fsanitize=fuzzer)
		endforeach()
	endif()
endif()
