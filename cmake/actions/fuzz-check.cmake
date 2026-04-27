## Replay all fuzz corpora (no new fuzzing, PR gate)
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/fuzz.cmake")
pal_fuzz_setup()

foreach(fuzz_exe corpus_dir seed_dir IN ZIP_LISTS fuzz_exes fuzz_corpus_dirs fuzz_seed_dirs)
    if(NOT IS_DIRECTORY "${corpus_dir}")
        continue()
    endif()
    message(STATUS "Checking ${fuzz_exe}")
    execute_process(
        COMMAND "${build_dir}/${fuzz_exe}" "${corpus_dir}" "${seed_dir}" -runs=0
        WORKING_DIRECTORY "${build_dir}"
        RESULT_VARIABLE result
    )
    if(result)
        message(FATAL_ERROR "Check failed for ${fuzz_exe} (crash in corpus?)")
    endif()
endforeach()
