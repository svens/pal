get_filename_component(_pal_fuzz_dir "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

macro(pal_fuzz_setup)
    set(project_root "${_pal_fuzz_dir}")
    include("${project_root}/pal/list.cmake")

    if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
        set(preset macos-clang-fuzz)
    else()
        set(preset linux-clang-fuzz)
    endif()

    set(build_dir "${project_root}/.build/${preset}")

    if(NOT IS_DIRECTORY "${build_dir}")
        execute_process(
            COMMAND ${CMAKE_COMMAND} --preset ${preset}
            WORKING_DIRECTORY "${project_root}"
            RESULT_VARIABLE result
        )
        if(result)
            message(FATAL_ERROR "Configure failed for ${preset}")
        endif()
    endif()

    set(fuzz_exes)
    set(fuzz_corpus_dirs)
    set(fuzz_seed_dirs)
    foreach(source IN LISTS pal_fuzz_sources)
        if(NOT source MATCHES "\\.fuzz\\.cpp$")
            continue()
        endif()
        string(REGEX REPLACE "\\.fuzz\\.cpp$" "" fuzz_path "${source}")
        string(REPLACE "/" "-" fuzz_exe "fuzz-${fuzz_path}")
        list(APPEND fuzz_exes "${fuzz_exe}")
        list(APPEND fuzz_corpus_dirs "${project_root}/fuzz/${fuzz_path}/corpus")
        list(APPEND fuzz_seed_dirs "${project_root}/fuzz/${fuzz_path}/seed")
    endforeach()

    execute_process(
        COMMAND ninja --quiet ${fuzz_exes}
        WORKING_DIRECTORY "${build_dir}"
        RESULT_VARIABLE result
    )
    if(result)
        message(FATAL_ERROR "Build failed")
    endif()
endmacro()

function(pal_fuzz_single source)
    set(project_root "${_pal_fuzz_dir}")

    if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
        set(preset macos-clang-fuzz)
    else()
        set(preset linux-clang-fuzz)
    endif()

    set(build_dir "${project_root}/.build/${preset}")

    if(NOT IS_DIRECTORY "${build_dir}")
        execute_process(
            COMMAND ${CMAKE_COMMAND} --preset ${preset}
            WORKING_DIRECTORY "${project_root}"
            RESULT_VARIABLE result
        )
        if(result)
            message(FATAL_ERROR "Configure failed for ${preset}")
        endif()
    endif()

    string(REGEX REPLACE "\\.fuzz\\.cpp$" "" fuzz_path "${source}")
    string(REPLACE "/" "-" fuzz_exe "fuzz-${fuzz_path}")
    set(corpus_dir "${project_root}/fuzz/${fuzz_path}/corpus")
    set(seed_dir "${project_root}/fuzz/${fuzz_path}/seed")

    execute_process(
        COMMAND ninja --quiet ${fuzz_exe}
        WORKING_DIRECTORY "${build_dir}"
        RESULT_VARIABLE result
    )
    if(result)
        message(FATAL_ERROR "Build failed for ${fuzz_exe}")
    endif()

    file(MAKE_DIRECTORY "${corpus_dir}")

    execute_process(
        COMMAND "${build_dir}/${fuzz_exe}" "${corpus_dir}" "${seed_dir}" -max_total_time=60
        WORKING_DIRECTORY "${build_dir}"
        RESULT_VARIABLE result
    )
    if(result)
        message(FATAL_ERROR "Fuzz run failed for ${fuzz_exe} (crash?)")
    endif()
endfunction()

if(DEFINED PAL_FUZZ_SOURCE)
    pal_fuzz_single("${PAL_FUZZ_SOURCE}")
endif()
