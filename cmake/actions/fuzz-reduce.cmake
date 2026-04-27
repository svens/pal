## Reduce all fuzz corpora (merge in-place)
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/fuzz.cmake")
pal_fuzz_setup()

foreach(fuzz_exe corpus_dir IN ZIP_LISTS fuzz_exes fuzz_corpus_dirs)
    if(NOT IS_DIRECTORY "${corpus_dir}")
        continue()
    endif()
    message(STATUS "Reducing ${fuzz_exe}")
    execute_process(
        COMMAND "${build_dir}/${fuzz_exe}" -merge=1 "${corpus_dir}" "${corpus_dir}"
        WORKING_DIRECTORY "${build_dir}"
        RESULT_VARIABLE result
    )
    if(result)
        message(FATAL_ERROR "Reduce failed for ${fuzz_exe}")
    endif()
endforeach()
