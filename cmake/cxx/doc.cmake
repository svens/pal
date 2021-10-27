function(cxx_doc name)
	cmake_parse_arguments(${name} "" "MAIN_PAGE;DIRECTORY" "" ${ARGN})

	message(CHECK_START "cxx_doc(${name})")
	list(APPEND CMAKE_MESSAGE_INDENT "    ")

	find_package(Doxygen)
	if(NOT DOXYGEN_FOUND)
		message(CHECK_FAIL "Doxygen not found")
		return()
	endif()

	set(doc_MAIN_PAGE ${${name}_MAIN_PAGE})
	set(doc_DIRECTORY ${${name}_DIRECTORY})
	configure_file(
		${CMAKE_CURRENT_SOURCE_DIR}/cmake/Doxyfile.in
		${CMAKE_BINARY_DIR}/Doxyfile
	)
	add_custom_target(${name}-doc
		COMMENT "Generate documentation"
		COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_BINARY_DIR}/Doxyfile
		WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
	)

	list(POP_BACK CMAKE_MESSAGE_INDENT)
	message(CHECK_PASS "done")
endfunction()
