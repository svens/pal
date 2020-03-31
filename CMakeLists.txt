cmake_minimum_required(VERSION 3.12)

project(pal LANGUAGES CXX)
include(cmake/project_version.cmake)
message(STATUS "${CMAKE_PROJECT_NAME} ${CMAKE_PROJECT_VERSION}")


# User settings {{{1

option(pal_unittests "Build unittests" ON)
option(pal_benchmarks "Build benchmarking application" OFF)
option(pal_docs "Generate documentation" OFF)

if(CMAKE_BUILD_TYPE MATCHES Coverage)
	set(CMAKE_BUILD_TYPE "Debug")
	set(Coverage ON)
	set(pal_unittests ON)
	set(pal_benchmarks OFF)
endif()


# Build settings {{{1

include(GNUInstallDirs)

# compiler
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
	include(cmake/g++.cmake)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
	include(cmake/clang++.cmake)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
	include(cmake/msvc.cmake)
else()
	message(WARNING "Unknown compiler")
endif()

# host
if(${CMAKE_SYSTEM_NAME} STREQUAL "Darwin")
	include(cmake/macos.cmake)
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
	include(cmake/linux.cmake)
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
	include(cmake/windows.cmake)
else()
	message(WARNING "Unknown system")
endif()


# Targets {{{1

include(pal/list.cmake)

add_library(pal ${pal_sources})
add_library(pal::pal ALIAS pal)
target_include_directories(pal
	PUBLIC
		$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
		$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_compile_options(pal PRIVATE ${max_warning_flags})

include(cmake/tidy.cmake)
tidy(pal)
include(cmake/iwyu.cmake)
iwyu(pal)


# Tests
if(pal_unittests)
	message(STATUS "Enable unittests")
	enable_testing()
	include(cmake/catchorg_catch2.cmake)
	add_executable(unittests ${pal_unittests_sources})
	target_compile_options(unittests PRIVATE ${max_warning_flags})
	target_link_libraries(unittests pal::pal Catch2::Catch2)
	include(extern/catchorg_catch2/contrib/Catch.cmake)
	catch_discover_tests(unittests)
endif()


# Benchmarks
if(pal_benchmarks)
	message(STATUS "Enable benchmarks")
	include(cmake/google_benchmark.cmake)
	include(bench/list.cmake)
	add_executable(benchmarks ${pal_benchmarks_sources})
	target_link_libraries(benchmarks pal::pal benchmark)
endif()


# Documentation
if(pal_docs)
	find_package(Doxygen)
	if(DOXYGEN_FOUND)
		message(STATUS "Enable documentation")
		configure_file(
			${CMAKE_CURRENT_SOURCE_DIR}/cmake/Doxyfile.in
			${CMAKE_BINARY_DIR}/Doxyfile
		)
		add_custom_target(${CMAKE_PROJECT_NAME}-doc
			${DOXYGEN_EXECUTABLE} ${CMAKE_BINARY_DIR}/Doxyfile
			WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
			COMMENT "Generating documentation"
		)
	else()
		message(STATUS "Disable documentation")
	endif()
endif()


# Packaging {{{1

set(CPACK_PACKAGE_VERSION "${CMAKE_PROJECT_VERSION}")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
list(APPEND CPACK_GENERATOR "TGZ")
list(APPEND CPACK_SOURCE_GENERATOR "TGZ")

# headers
foreach(file ${pal_sources})
	if(NOT ${file} MATCHES ".cpp$")
		get_filename_component(path ${file} PATH)
		install(FILES ${file}
			DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${path}
			COMPONENT dev
		)
	endif()
endforeach()
list(APPEND CPACK_COMPONENTS_ALL dev)

# library
install(TARGETS pal
	ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
	COMPONENT lib
)
list(APPEND CPACK_COMPONENTS_ALL lib)

include(CPack)
