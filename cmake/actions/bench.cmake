## Build release preset and run benchmarks
get_filename_component(project_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    set(preset windows-msvc-release)
    set(exe_suffix .exe)
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    set(preset macos-clang-release)
    set(exe_suffix "")
else()
    set(preset linux-gcc-release)
    set(exe_suffix "")
endif()

set(build_dir "${project_root}/.build/${preset}")

if(NOT IS_DIRECTORY "${build_dir}")
    execute_process(
        COMMAND ${CMAKE_COMMAND} --preset ${preset}
        WORKING_DIRECTORY "${project_root}"
        RESULT_VARIABLE result
    )
    if(result)
        message(FATAL_ERROR "cmake --preset ${preset} failed")
    endif()
endif()

execute_process(
    COMMAND ninja --quiet
    WORKING_DIRECTORY "${build_dir}"
    RESULT_VARIABLE result
)
if(result)
    message(FATAL_ERROR "Build failed")
endif()

execute_process(
    COMMAND "${build_dir}/pal_test${exe_suffix}" "[!benchmark]" --order decl --benchmark-samples 200
    WORKING_DIRECTORY "${build_dir}"
    RESULT_VARIABLE result
)
if(result)
    message(FATAL_ERROR "Benchmarks failed")
endif()
