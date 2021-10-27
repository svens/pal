macro(cxx_benchmark name)
	cmake_parse_arguments(${name} "" "" "SOURCES;LIBRARIES" ${ARGN})

	message(CHECK_START "cxx_benchmark(${name})")
	list(APPEND CMAKE_MESSAGE_INDENT "    ")

	message(VERBOSE "Fetch google/benchmark")
	include(FetchContent)
	FetchContent_Declare(Benchmark
		URL https://github.com/google/benchmark/archive/v1.6.0.tar.gz
	)
	set(BENCHMARK_ENABLE_TESTING OFF)
	set(BENCHMARK_ENABLE_INSTALL OFF)
	FetchContent_MakeAvailable(Benchmark)
	FetchContent_GetProperties(Benchmark SOURCE_DIR Benchmark_SOURCE_DIR)

	add_executable(${name} ${${name}_SOURCES})
	target_compile_options(${name} PRIVATE ${cxx_max_warning_flags})
	target_include_directories(${name} SYSTEM PRIVATE ${Benchmark_SOURCE_DIR}/include)
	target_link_libraries(${name} benchmark ${${name}_LIBRARIES})

	list(POP_BACK CMAKE_MESSAGE_INDENT)
	message(CHECK_PASS "done")
endmacro()
