## Build, test, generate LCOV coverage report
include("${CMAKE_CURRENT_LIST_DIR}/../coverage.cmake")

coverage_collect()

coverage_process(export
    --format=lcov
    OUTPUT_FILE "${_cov_build_dir}/coverage.lcov"
)

message(STATUS "Coverage LCOV: ${_cov_build_dir}/coverage.lcov")
