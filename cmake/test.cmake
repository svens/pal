enable_testing()

include(FetchContent)
FetchContent_Declare(Catch2
	GIT_REPOSITORY https://github.com/catchorg/Catch2.git
	GIT_TAG v3.13.0
	GIT_SHALLOW ON
)
FetchContent_MakeAvailable(Catch2)
FetchContent_GetProperties(Catch2 SOURCE_DIR Catch2_SOURCE_DIR)
list(APPEND CMAKE_MODULE_PATH ${Catch2_SOURCE_DIR}/extras)
include(Catch)

add_library(cxx_test INTERFACE)
target_include_directories(cxx_test SYSTEM INTERFACE ${Catch2_SOURCE_DIR}/src)
target_compile_definitions(cxx_test INTERFACE CATCH_CONFIG_NO_COUNTER)
target_link_libraries(cxx_test INTERFACE cxx_warnings Catch2::Catch2)

