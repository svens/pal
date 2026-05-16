## Build, test, generate coverage report, and format as HTML
include("${CMAKE_CURRENT_LIST_DIR}/../coverage.cmake")

coverage_collect()

coverage_process(show
    -format=html
    -output-dir="${_cov_build_dir}/coverage"
    -show-instantiation-summary
    --Xdemangler=c++filt
    --Xdemangler=-n
)

message(STATUS "Coverage report: .build/${_cov_preset}/coverage/index.html")
