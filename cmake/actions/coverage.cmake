## Build, test, and generate HTML coverage report
get_filename_component(project_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

find_program(llvm_profdata llvm-profdata)
if(NOT llvm_profdata)
    message(FATAL_ERROR "llvm-profdata not found")
endif()

find_program(llvm_cov llvm-cov)
if(NOT llvm_cov)
    message(FATAL_ERROR "llvm-cov not found")
endif()

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    set(preset macos-clang-coverage)
else()
    set(preset linux-clang-coverage)
endif()

set(build_dir "${project_root}/.build/${preset}")

file(GLOB old_profraw "${build_dir}/pal_test-*.profraw")
if(old_profraw)
    file(REMOVE ${old_profraw})
endif()

execute_process(
    COMMAND ${CMAKE_COMMAND} --workflow --preset ${preset}
    WORKING_DIRECTORY "${project_root}"
    RESULT_VARIABLE result
)
if(result)
    message(FATAL_ERROR "cmake --workflow --preset ${preset} failed")
endif()

file(GLOB profraw_files "${build_dir}/pal_test-*.profraw")
if(NOT profraw_files)
    message(FATAL_ERROR "No .profraw files found in ${build_dir}")
endif()

execute_process(
    COMMAND ${llvm_profdata} merge -sparse ${profraw_files}
        -o "${build_dir}/coverage.profdata"
    RESULT_VARIABLE result
)
if(result)
    message(FATAL_ERROR "llvm-profdata failed")
endif()

set(report_dir "${build_dir}/coverage")
file(MAKE_DIRECTORY "${report_dir}")

execute_process(
    COMMAND ${llvm_cov} show "${build_dir}/pal_test"
        "-instr-profile=${build_dir}/coverage.profdata"
        -format=html
        "-output-dir=${report_dir}"
        -show-instantiation-summary
        --Xdemangler=c++filt
        --Xdemangler=-n
        "-ignore-filename-regex=.*(test|bench)\\.cpp$"
        "-ignore-filename-regex=.*/test\\.(cpp|hpp)$"
        "-ignore-filename-regex=.*\\.build/.*"
    WORKING_DIRECTORY "${project_root}"
    RESULT_VARIABLE result
)
if(result)
    message(FATAL_ERROR "llvm-cov failed")
endif()

message(STATUS "Coverage report: .build/${preset}/coverage/index.html")
