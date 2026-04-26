## Regenerate build.ninja
get_filename_component(project_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

file(GLOB action_files "${CMAKE_CURRENT_LIST_DIR}/*.cmake")
list(SORT action_files)

set(content "ninja_required_version = 1.1\n")
string(APPEND content "\nrule action\n")
string(APPEND content "  command = cmake -P cmake/actions/$name.cmake\n")
string(APPEND content "  description = $name\n")
string(APPEND content "  pool = console\n")

foreach(action_file IN LISTS action_files)
    get_filename_component(name "${action_file}" NAME_WE)
    string(APPEND content "\nbuild ${name}: action\n")
    string(APPEND content "  name = ${name}\n")
endforeach()

string(APPEND content "\nrule fuzz-action\n")
string(APPEND content "  command = cmake -DPAL_FUZZ_SOURCE=$fuzz_source -P cmake/fuzz.cmake\n")
string(APPEND content "  description = $out\n")
string(APPEND content "  pool = console\n")

include("${project_root}/pal/list.cmake")
foreach(source IN LISTS pal_fuzz_sources)
    if(NOT source MATCHES "\\.fuzz\\.cpp$")
        continue()
    endif()
    string(REGEX REPLACE "\\.fuzz\\.cpp$" "" fuzz_path "${source}")
    string(APPEND content "\nbuild fuzz/${fuzz_path}: fuzz-action\n")
    string(APPEND content "  fuzz_source = ${source}\n")
endforeach()

string(APPEND content "\ndefault all\n")

file(WRITE "${project_root}/build.ninja" "${content}")
message(STATUS "Regenerated build.ninja")
