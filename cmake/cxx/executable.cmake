macro(cxx_executable name)
	cmake_parse_arguments(${name} "" "" "SOURCES;LIBRARIES" ${ARGN})

	message(CHECK_START "cxx_executable(${name})")
	list(APPEND CMAKE_MESSAGE_INDENT "    ")

	add_executable(${name} ${${name}_SOURCES})
	target_compile_options(${name} PRIVATE ${cxx_max_warning_flags})
	target_link_libraries(${name} ${${name}_LIBRARIES})

	list(POP_BACK CMAKE_MESSAGE_INDENT)
	message(CHECK_PASS "done")
endmacro()
