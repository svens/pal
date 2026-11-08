if(APPLE)
	execute_process(
		COMMAND xcrun --show-sdk-path
		OUTPUT_VARIABLE _tidy_apple_sdk_path
		OUTPUT_STRIP_TRAILING_WHITESPACE
	)
	set(_tidy_extra_args
		-extra-arg=-isysroot${_tidy_apple_sdk_path}
		-extra-arg=-isystem${OPENSSL_INCLUDE_DIR}
	)
endif()

find_program(RUN_CLANG_TIDY run-clang-tidy)
if(RUN_CLANG_TIDY)
	add_custom_target(tidy_src
		COMMAND ${RUN_CLANG_TIDY}
			-p ${CMAKE_BINARY_DIR}
			-source-filter "^${CMAKE_SOURCE_DIR}/pal/(?!.*\\.(test|bench|fuzz)\\.cpp$).*\\.cpp$"
			-quiet
			-hide-progress
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
			-hide-progress
			${_tidy_extra_args}
		WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
		VERBATIM
	)
	set(_tidy_targets tidy_src tidy_test)
	if(PAL_SAMPLES)
		add_custom_target(tidy_samples
			COMMAND ${RUN_CLANG_TIDY}
				-p ${CMAKE_BINARY_DIR}
				-source-filter "^${CMAKE_SOURCE_DIR}/samples/.*\\.cpp$"
				-quiet
				-hide-progress
				${_tidy_extra_args}
			WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
			VERBATIM
		)
		list(APPEND _tidy_targets tidy_samples)
	endif()
	add_custom_target(tidy)
	add_dependencies(tidy ${_tidy_targets})
else()
	add_custom_target(tidy
		COMMAND ${CMAKE_COMMAND} -E echo "run-clang-tidy not found"
		COMMAND ${CMAKE_COMMAND} -E false
	)
endif()
