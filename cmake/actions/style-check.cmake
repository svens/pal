## Check mechanical source style (indentation, whitespace, encoding)
get_filename_component(project_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

include("${project_root}/pal/list.cmake")
include("${project_root}/samples/list.cmake")

# Content is never split into a CMake list: ';' and '\' are common in C++ and
# both are list/escape metacharacters. Lines are walked with string(SUBSTRING)
# and every expansion is quoted, so source text stays inert.
function(style_report path content pattern reason)
    set(lineno 1)
    set(rest "${content}")
    while(NOT rest STREQUAL "")
        string(FIND "${rest}" "\n" nl)
        if(nl LESS 0)
            set(line "${rest}")
            set(rest "")
        else()
            string(SUBSTRING "${rest}" 0 ${nl} line)
            math(EXPR skip "${nl} + 1")
            string(SUBSTRING "${rest}" ${skip} -1 rest)
        endif()
        if("\n${line}\n" MATCHES "${pattern}")
            list(APPEND violations "${path}:${lineno}: ${reason}")
        endif()
        math(EXPR lineno "${lineno} + 1")
    endwhile()
    set(violations "${violations}" PARENT_SCOPE)
endfunction()

set(non_ascii "[^\t\n -~]")
set(trailing_space "[ \t]+\n")
set(space_indent "\n\t* +[^*\n]")

set(violations "")

foreach(source IN LISTS pal_sources pal_test_sources pal_fuzz_sources samples_sources)
    file(READ "${project_root}/${source}" content)
    string(LENGTH "${content}" length)

    # file(READ) reads in text mode and silently drops CR, so a carriage return is
    # invisible to the regexes below. The size it did not return is the count.
    file(SIZE "${project_root}/${source}" size)
    if(NOT size EQUAL length)
        math(EXPR dropped "${size} - ${length}")
        list(APPEND violations "${source}: ${dropped} carriage return(s)")
    endif()

    # Whole-file match first; the per-line walk runs only to locate a known violation.
    if(content MATCHES "${non_ascii}")
        style_report("${source}" "${content}" "${non_ascii}" "non-ASCII character")
    endif()

    if(content MATCHES "${trailing_space}")
        style_report("${source}" "${content}" "${trailing_space}" "trailing whitespace")
    endif()

    if(content MATCHES "${space_indent}")
        style_report("${source}" "${content}" "${space_indent}" "space indentation")
    endif()

    if(length GREATER 0)
        math(EXPR last "${length} - 1")
        string(SUBSTRING "${content}" ${last} 1 tail)
        if(NOT tail STREQUAL "\n")
            list(APPEND violations "${source}: missing final newline")
        endif()
    endif()
endforeach()

if(violations)
    list(JOIN violations "\n" report)
    list(LENGTH violations count)
    message("${report}")
    message(FATAL_ERROR "Style check failed: ${count} violation(s)")
endif()
