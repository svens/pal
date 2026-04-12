pal_system_libs_add(
	WINDOWS ws2_32
)

configure_file(
	pal/version.cpp.in
	version.cpp
	@ONLY
)
list(APPEND pal_sources ${CMAKE_CURRENT_BINARY_DIR}/version.cpp)

list(APPEND pal_sources
	pal/byte_order.hpp
	pal/error.hpp
	pal/error.cpp
	pal/hash.hpp
	pal/intrusive_queue.hpp
	pal/intrusive_stack.hpp
	pal/result.hpp
	pal/span.hpp
	pal/version.hpp
)

list(APPEND pal_test_sources
	pal/test.hpp
	pal/test.cpp
	pal/byte_order.test.cpp
	pal/error.test.cpp
	pal/hash.bench.cpp
	pal/hash.test.cpp
	pal/intrusive_queue.test.cpp
	pal/intrusive_stack.test.cpp
	pal/result.test.cpp
	pal/span.test.cpp
)
