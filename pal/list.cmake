configure_file(
	pal/version.cpp.in
	version.cpp
	@ONLY
)
list(APPEND pal_sources ${CMAKE_CURRENT_BINARY_DIR}/version.cpp)

list(APPEND pal_sources
	pal/__expected
	pal/__platform_sdk
	pal/byte_order
	pal/conv
	pal/conv_hex.cpp
	pal/error
	pal/error.cpp
	pal/hash
	pal/result
	pal/span
	pal/string
	pal/version
)

list(APPEND pal_test_sources
	pal/test
	pal/test.cpp
	pal/byte_order.test.cpp
	pal/conv.bench.cpp
	pal/conv.test.cpp
	pal/hash.bench.cpp
	pal/hash.test.cpp
	pal/error.test.cpp
	pal/result.test.cpp
	pal/span.test.cpp
)

include(pal/crypto/list.cmake)
include(pal/net/list.cmake)
