## Build and smoke-test one preset
get_filename_component(project_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    set(preset windows-msvc-debug)
    set(exe_suffix .exe)
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    set(preset macos-clang-debug)
    set(exe_suffix "")
else()
    set(preset linux-clang-debug)
    set(exe_suffix "")
endif()

set(build_dir "${project_root}/.build/${preset}")

if(NOT IS_DIRECTORY "${build_dir}")
    message(FATAL_ERROR "${build_dir} not found -- run 'ninja' first to configure all presets")
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
    COMMAND "${build_dir}/pal_test${exe_suffix}" --skip-benchmarks
    WORKING_DIRECTORY "${build_dir}"
    RESULT_VARIABLE result
)
if(result)
    message(FATAL_ERROR "Tests failed")
endif()
