## Check source formatting (dry-run)
get_filename_component(project_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

find_program(clang_format clang-format)
if(NOT clang_format)
    message(FATAL_ERROR "clang-format not found")
endif()

include("${project_root}/pal/list.cmake")

execute_process(
    COMMAND ${clang_format} --dry-run --Werror ${pal_sources} ${pal_test_sources} ${pal_fuzz_sources}
    WORKING_DIRECTORY "${project_root}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE
)
if(result)
    if(stdout)
        message("${stdout}")
    endif()
    message(FATAL_ERROR "Formatting check failed:\n${stderr}")
endif()
