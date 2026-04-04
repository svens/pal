find_program(RUN_CLANG_TIDY run-clang-tidy)
if(RUN_CLANG_TIDY)
	add_custom_target(tidy
		COMMAND ${RUN_CLANG_TIDY}
			-p ${CMAKE_BINARY_DIR}
			-source-filter "^${CMAKE_SOURCE_DIR}/pal/"
			-quiet
		WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
		VERBATIM
	)
endif()
