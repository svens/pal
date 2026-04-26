if(APPLE)
	execute_process(
		COMMAND xcrun --show-sdk-path
		OUTPUT_VARIABLE _tidy_apple_sdk_path
		OUTPUT_STRIP_TRAILING_WHITESPACE
	)
	set(_tidy_extra_args
		-extra-arg=-isysroot
		-extra-arg=${_tidy_apple_sdk_path}
	)
endif()

find_program(RUN_CLANG_TIDY run-clang-tidy)
if(RUN_CLANG_TIDY)
	add_custom_target(tidy_src
		COMMAND ${RUN_CLANG_TIDY}
			-p ${CMAKE_BINARY_DIR}
			-source-filter "^${CMAKE_SOURCE_DIR}/pal/(?!.*\\.(test|bench|fuzz)\\.cpp$).*\\.cpp$"
			-quiet
			${_tidy_extra_args}
		WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
		VERBATIM
	)
	add_custom_target(tidy_test
		COMMAND ${RUN_CLANG_TIDY}
			-p ${CMAKE_BINARY_DIR}
			-source-filter "^${CMAKE_SOURCE_DIR}/pal/.*\\.(test|bench|fuzz)\\.cpp$"
			-config-file ${CMAKE_SOURCE_DIR}/.clang-tidy-test
			-quiet
			${_tidy_extra_args}
		WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
		VERBATIM
	)
	add_custom_target(tidy)
	add_dependencies(tidy tidy_src tidy_test)
else()
	add_custom_target(tidy
		COMMAND ${CMAKE_COMMAND} -E echo "run-clang-tidy not found"
		COMMAND ${CMAKE_COMMAND} -E false
	)
endif()
