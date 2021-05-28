cmake_minimum_required(VERSION 3.12)

project(pal LANGUAGES CXX)
include(cmake/project_version.cmake)
message(STATUS "${CMAKE_PROJECT_NAME} ${CMAKE_PROJECT_VERSION}")

option(pal_test "Build tests" ON)
option(pal_cov "Generate test coverage" OFF)
option(pal_doc "Generate documentation" OFF)
option(pal_bench "Build benchmarking application" OFF)

set(pal_features)
if(pal_test)
    list(APPEND pal_features WITH_TEST)
endif()
if(pal_cov)
    list(APPEND pal_features WITH_COV)
endif()
if(pal_doc)
    list(APPEND pal_features WITH_DOC)
endif()

if(pal_test OR pal_bench)
    if(${CMAKE_SYSTEM_NAME} STREQUAL "Darwin")
        # MacOS
        find_library(core_foundation_lib CoreFoundation)
        find_library(security_lib Security)
        set(pal_libs WITH_LIBS ${core_foundation_lib} ${security_lib})
    elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
        # Linux
        find_package(Threads REQUIRED)
        find_package(OpenSSL REQUIRED)
        list(FIND CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES OPENSSL_INCLUDE_DIR i)
        if(${i} EQUAL -1)
            include_directories(${OPENSSL_INCLUDE_DIR})
        endif()
        set(pal_libs WITH_LIBS ${OPENSSL_CRYPTO_LIBRARY} Threads::Threads)
    elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
        # Windows
        set(pal_libs WITH_LIBS bcrypt crypt32 ncrypt ntdll ws2_32)
    endif()
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(cmake/cxx.cmake)
cmake_cxx_add_library(pal
    LIST_FILE pal/list.cmake
    ${pal_features}
    ${pal_libs}
)

if(pal_bench)
    cmake_cxx_add_benchmark(pal
        LIST_FILE bench/list.cmake
        ${pal_libs} pal::pal
    )
endif()
