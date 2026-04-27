## List available fuzz targets
get_filename_component(project_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
include("${project_root}/pal/list.cmake")

foreach(source IN LISTS pal_fuzz_sources)
    if(NOT source MATCHES "\\.fuzz\\.cpp$")
        continue()
    endif()
    string(REGEX REPLACE "\\.fuzz\\.cpp$" "" fuzz_path "${source}")
    message("  fuzz/${fuzz_path}")
endforeach()
