## Run clang-tidy
get_filename_component(project_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    set(preset macos-clang-debug)
else()
    set(preset linux-clang-debug)
endif()

execute_process(
    COMMAND ${CMAKE_COMMAND} --preset ${preset}
    WORKING_DIRECTORY "${project_root}"
    RESULT_VARIABLE result
)
if(result)
    message(FATAL_ERROR "cmake --preset ${preset} failed")
endif()

execute_process(
    COMMAND ${CMAKE_COMMAND} --build --preset ${preset} --target tidy
    WORKING_DIRECTORY "${project_root}"
    RESULT_VARIABLE result
)
if(result)
    message(FATAL_ERROR "cmake --build --preset ${preset} --target tidy failed")
endif()
