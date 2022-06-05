cmake_minimum_required(VERSION 3.12)

project(pal LANGUAGES CXX)
include(cmake/project_version.cmake)
message(STATUS "${CMAKE_PROJECT_NAME} ${CMAKE_PROJECT_VERSION}")

option(pal_test "Build tests" ON)
option(pal_bench "Build benchmarks" OFF)
option(pal_doc "Generate documentation" OFF)
option(pal_samples "Build samples" OFF)

if(NOT "${CMAKE_BUILD_TYPE};${CMAKE_CONFIGURATION_TYPES}" MATCHES ".*Coverage.*")
	set(xxx_test ON)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(cmake/cxx.cmake)

# dependencies {{{1
if(${CMAKE_SYSTEM_NAME} STREQUAL "Darwin")
	find_library(core_foundation_lib CoreFoundation)
	find_library(security_lib Security)
	set(pal_libraries ${core_foundation_lib} ${security_lib})
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
	find_package(Threads REQUIRED)
	find_package(OpenSSL REQUIRED)
	list(FIND CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES OPENSSL_INCLUDE_DIR i)
	if(${i} EQUAL -1)
		include_directories(SYSTEM ${OPENSSL_INCLUDE_DIR})
	endif()
	set(pal_libraries OpenSSL::Crypto Threads::Threads)
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
	set(pal_libraries bcrypt crypt32 ncrypt ntdll ws2_32)
endif()

# library {{{1
include(pal/list.cmake)
cxx_library(pal
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

# benchmarks {{{1
if(pal_bench)
	include(bench/list.cmake)
	cxx_benchmark(pal_bench
		SOURCES ${pal_bench_sources}
		LIBRARIES pal::pal
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
