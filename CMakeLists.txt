cmake_minimum_required(VERSION 3.12)

project(pal LANGUAGES CXX)
include(cmake/project_version.cmake)
message(STATUS "${PROJECT_NAME} ${PROJECT_VERSION}")

option(pal_test "Build tests" ON)
option(pal_doc "Generate documentation" OFF)
option(pal_samples "Build samples" OFF)

if("${CMAKE_BUILD_TYPE};${CMAKE_CONFIGURATION_TYPES}" MATCHES ".*Coverage.*")
	set(pal_test ON)
endif()

if(NOT CMAKE_CXX_STANDARD)
	set(CMAKE_CXX_STANDARD 20)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(cmake/cxx.cmake)

set(pal_libraries)
if(${CMAKE_SYSTEM_NAME} STREQUAL "Darwin") #{{{1
	#
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Linux") #{{{1
	find_package(OpenSSL REQUIRED)
	list(FIND CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES OPENSSL_INCLUDE_DIR i)
	if(${i} EQUAL -1)
		include_directories(SYSTEM ${OPENSSL_INCLUDE_DIR})
	endif()
	set(pal_libraries ${pal_libraries} OpenSSL::Crypto)
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Windows") #{{{1
	set(pal_libraries ${pal_libraries} bcrypt ntdll ws2_32)
endif()
# }}}1

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
	if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
		# did not find root cause, this is workaround
		# https://stackoverflow.com/questions/14125007/gcc-string-table-overflow-error-during-compilation
		target_compile_options(pal_test PRIVATE /GL)
	endif()
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
