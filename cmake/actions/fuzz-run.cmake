## Run all fuzz targets (extended campaign)
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/fuzz.cmake")
pal_fuzz_setup()

foreach(fuzz_exe corpus_dir seed_dir IN ZIP_LISTS fuzz_exes fuzz_corpus_dirs fuzz_seed_dirs)
    file(MAKE_DIRECTORY "${corpus_dir}")
    message(STATUS "Running ${fuzz_exe}")
    execute_process(
        COMMAND "${build_dir}/${fuzz_exe}" "${corpus_dir}" "${seed_dir}" -max_total_time=300
        WORKING_DIRECTORY "${build_dir}"
        RESULT_VARIABLE result
    )
    if(result)
        message(FATAL_ERROR "Run failed for ${fuzz_exe} (crash?)")
    endif()
endforeach()
