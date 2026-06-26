## Build and test with ASan/UBSan
get_filename_component(project_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    set(preset macos-clang-sanitize)
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    set(preset windows-msvc-sanitize)
else()
    set(preset linux-gcc-sanitize)
endif()

execute_process(
    COMMAND ${CMAKE_COMMAND} --workflow --preset ${preset}
    WORKING_DIRECTORY "${project_root}"
    RESULT_VARIABLE result
)
if(result)
    message(FATAL_ERROR "cmake --workflow --preset ${preset} failed")
endif()
