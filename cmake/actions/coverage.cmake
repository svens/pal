## Build, test, and generate HTML coverage report
get_filename_component(project_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

find_program(gcovr gcovr)
if(NOT gcovr)
    message(FATAL_ERROR "gcovr not found")
endif()

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    set(preset macos-gcc-coverage)
else()
    set(preset linux-gcc-coverage)
endif()

execute_process(
    COMMAND ${CMAKE_COMMAND} --workflow --preset ${preset}
    WORKING_DIRECTORY "${project_root}"
    RESULT_VARIABLE result
)
if(result)
    message(FATAL_ERROR "cmake --workflow --preset ${preset} failed")
endif()

set(report_dir "${project_root}/.build/${preset}/coverage")
file(MAKE_DIRECTORY "${report_dir}")

execute_process(
    COMMAND ${gcovr} "${project_root}/.build/${preset}"
        --filter "pal/"
        -e ".*\\.test\\.cpp$"
        -e ".*\\.bench\\.cpp$"
        -e ".*/test\\.(cpp|hpp)$"
        --merge-mode-functions separate
        --html-details "${report_dir}/index.html"
    WORKING_DIRECTORY "${project_root}"
    RESULT_VARIABLE result
    ERROR_VARIABLE stderr
    ERROR_STRIP_TRAILING_WHITESPACE
)
if(result)
    message(FATAL_ERROR "gcovr failed:\n${stderr}")
endif()

message(STATUS "Coverage report: .build/${preset}/coverage/index.html")
