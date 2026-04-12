set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(cxx_warnings INTERFACE)
target_compile_options(cxx_warnings INTERFACE
	$<$<CXX_COMPILER_ID:GNU,Clang>:-Werror -Wall -Wextra -Weffc++ -pedantic>
	$<$<CXX_COMPILER_ID:MSVC>:/W4 /WX /w34265 /w34777 /w34946 /w35038>
)
