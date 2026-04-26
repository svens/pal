enable_testing()

include(FetchContent)
FetchContent_Declare(Catch2
	URL https://github.com/catchorg/Catch2/archive/refs/tags/v3.14.0.tar.gz
	URL_HASH SHA256=ba2a939efead3c833c499cf487e185762f419a71d30158cd1b43c6079c586490
)
FetchContent_MakeAvailable(Catch2)
FetchContent_GetProperties(Catch2 SOURCE_DIR Catch2_SOURCE_DIR)
list(APPEND CMAKE_MODULE_PATH ${Catch2_SOURCE_DIR}/extras)
include(Catch)

add_library(cxx_test INTERFACE)
target_include_directories(cxx_test SYSTEM INTERFACE ${Catch2_SOURCE_DIR}/src)
target_compile_definitions(cxx_test INTERFACE
	CATCH_CONFIG_NO_COUNTER
	$<$<BOOL:${WIN32}>:NOMINMAX WIN32_LEAN_AND_MEAN>
)
target_link_libraries(cxx_test INTERFACE cxx_warnings Catch2::Catch2)

