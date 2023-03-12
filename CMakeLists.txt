cmake_minimum_required(VERSION 3.12)

project(pal LANGUAGES CXX)
include(cmake/project_version.cmake)
message(STATUS "${PROJECT_NAME} ${CMAKE_PROJECT_VERSION}")

option(pal_test "Build tests" ON)
option(pal_doc "Generate documentation" OFF)
option(pal_samples "Build samples" OFF)

if(NOT "${CMAKE_BUILD_TYPE};${CMAKE_CONFIGURATION_TYPES}" MATCHES ".*Coverage.*")
	set(pal_test ON)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(cmake/cxx.cmake)

set(pal_libraries)
if(${CMAKE_SYSTEM_NAME} STREQUAL "Darwin")
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
	set(pal_libraries ${pal_libraries} ws2_32)
endif()

# library {{{1
include(pal/list.cmake)
cxx_library(pal
	ALIAS pal::pal
	SOURCES ${pal_sources}
	LIBRARIES ${pal_libraries}
)

# tests (+coverage) {{{1
if(pal_test)
	cxx_test(pal_test
		SOURCES ${pal_test_sources}
		LIBRARIES pal::pal
		COVERAGE_BASE_DIR ${CMAKE_SOURCE_DIR}/pal
	)
endif()

# documentation {{{1
if(pal_doc)
	cxx_doc(pal
		MAIN_PAGE ${CMAKE_SOURCE_DIR}/README.md
		DIRECTORY ${CMAKE_SOURCE_DIR}/pal
	)
endif()

# samples {{{1
if(pal_samples)
	include(samples/list.cmake)
endif()
