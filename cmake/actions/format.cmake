## Format all source files
get_filename_component(project_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

find_program(clang_format clang-format)
if(NOT clang_format)
    message(FATAL_ERROR "clang-format not found")
endif()

include("${project_root}/pal/list.cmake")

execute_process(
    COMMAND ${clang_format} -i ${pal_sources} ${pal_test_sources}
    WORKING_DIRECTORY "${project_root}"
    RESULT_VARIABLE result
    ERROR_VARIABLE stderr
    ERROR_STRIP_TRAILING_WHITESPACE
)
if(result)
    message(FATAL_ERROR "clang-format failed:\n${stderr}")
endif()
