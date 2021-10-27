# https://docs.microsoft.com/en-us/cpp/preprocessor/compiler-warnings-that-are-off-by-default
set(cxx_max_warning_flags /W4 /WX /w34265 /w34777 /w34946 /w35038)
string(REGEX REPLACE "/W3" "" CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS})
