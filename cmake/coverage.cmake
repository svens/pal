## Internal: build, test and collect coverage data (profdata)

include_guard()

get_filename_component(_cov_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

set(_cov_ignore
    "-ignore-filename-regex=.*(test|bench)\\.cpp$"
    "-ignore-filename-regex=.*/test\\.(cpp|hpp)$"
    "-ignore-filename-regex=.*\\.build/.*"
)

macro(coverage_collect)
    find_program(_cov_llvm_profdata llvm-profdata REQUIRED)
    find_program(_cov_llvm_cov llvm-cov REQUIRED)

    if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
        set(_cov_preset macos-clang-coverage)
    else()
        set(_cov_preset linux-clang-coverage)
    endif()

    set(_cov_build_dir "${_cov_root}/.build/${_cov_preset}")
    set(_cov_binary "${_cov_build_dir}/pal_test")
    set(_cov_profdata "${_cov_build_dir}/coverage.profdata")

    file(GLOB _old_profraw "${_cov_build_dir}/pal_test-*.profraw")
    if(_old_profraw)
        file(REMOVE ${_old_profraw})
    endif()

    execute_process(
        COMMAND ${CMAKE_COMMAND} --workflow --preset ${_cov_preset}
        WORKING_DIRECTORY "${_cov_root}"
        RESULT_VARIABLE _cov_result
    )
    if(_cov_result)
        message(FATAL_ERROR "cmake --workflow --preset ${_cov_preset} failed")
    endif()

    file(GLOB _profraw_files "${_cov_build_dir}/pal_test-*.profraw")
    if(NOT _profraw_files)
        message(FATAL_ERROR "No .profraw files found in ${_cov_build_dir}")
    endif()

    execute_process(
        COMMAND ${_cov_llvm_profdata} merge -sparse ${_profraw_files}
            -o "${_cov_profdata}"
        RESULT_VARIABLE _cov_result
    )
    if(_cov_result)
        message(FATAL_ERROR "llvm-profdata failed")
    endif()
endmacro()

function(coverage_process)
    cmake_parse_arguments(PARSE_ARGV 0 _CP "" "OUTPUT_FILE" "")

    set(_extra)
    if(_CP_OUTPUT_FILE)
        list(APPEND _extra OUTPUT_FILE "${_CP_OUTPUT_FILE}")
    endif()

    execute_process(
        COMMAND ${_cov_llvm_cov} ${_CP_UNPARSED_ARGUMENTS}
            "${_cov_binary}"
            "-instr-profile=${_cov_profdata}"
            ${_cov_ignore}
        ${_extra}
        WORKING_DIRECTORY "${_cov_root}"
        RESULT_VARIABLE result
    )
    if(result)
        message(FATAL_ERROR "llvm-cov failed")
    endif()

    if(NOT _CP_OUTPUT_FILE)
        return()
    endif()

    find_program(_cp_awk awk)
    if(NOT _cp_awk)
        message(WARNING "awk not found -- LCOV_EXCL markers not processed")
        return()
    endif()

    set(_cp_awk_script "${_cov_build_dir}/filter_lcov.awk")
    file(WRITE "${_cp_awk_script}" [[
/^SF:/ {
    source = substr($0, 4)
    delete excluded
    in_excl = 0
    lineno = 0
    while ((getline src_line < source) > 0) {
        lineno++
        if (index(src_line, "LCOV_EXCL_START") > 0) in_excl = 1
        if (in_excl) excluded[lineno] = 1
        if (index(src_line, "LCOV_EXCL_STOP") > 0) in_excl = 0
    }
    close(source)
    print
    next
}
/^DA:/ {
    da = substr($0, 4)
    n = substr(da, 1, index(da, ",") - 1)
    if (n in excluded) next
    print
    next
}
{ print }
]])

    set(_cp_filtered "${_CP_OUTPUT_FILE}.tmp")
    execute_process(
        COMMAND ${_cp_awk} -f "${_cp_awk_script}" "${_CP_OUTPUT_FILE}"
        OUTPUT_FILE "${_cp_filtered}"
        RESULT_VARIABLE result
    )
    if(result)
        message(FATAL_ERROR "LCOV filtering failed")
    endif()
    file(RENAME "${_cp_filtered}" "${_CP_OUTPUT_FILE}")
endfunction()
