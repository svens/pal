## Build and test all (platform-specific combinations)
get_filename_component(project_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    set(presets windows-msvc-debug windows-msvc-release)
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    set(presets macos-clang-debug macos-clang-release macos-gcc-debug macos-gcc-release)
else()
    set(presets linux-gcc-debug linux-gcc-release linux-clang-debug linux-clang-release)
endif()

foreach(preset IN LISTS presets)
    execute_process(
        COMMAND ${CMAKE_COMMAND} --workflow --preset ${preset}
        WORKING_DIRECTORY "${project_root}"
        RESULT_VARIABLE result
    )
    if(result)
        message(FATAL_ERROR "cmake --workflow --preset ${preset} failed")
    endif()
endforeach()
