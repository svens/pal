configure_file(
	pal/version.cpp.in
	version.cpp
	@ONLY
)
list(APPEND pal_sources ${CMAKE_CURRENT_BINARY_DIR}/version.cpp)

list(APPEND pal_sources
	pal/__expected
	pal/__platform_sdk
	pal/assert
	pal/byte_order
	pal/conv
	pal/conv_base64.cpp
	pal/conv_hex.cpp
	pal/error
	pal/error.cpp
	pal/hash
	pal/intrusive_queue
	pal/memory
	pal/not_null
	pal/result
	pal/span
	pal/string
	pal/version
)

list(APPEND pal_test_sources
	pal/test
	pal/test.cpp
	pal/assert.test.cpp
	pal/byte_order.test.cpp
	pal/conv.bench.cpp
	pal/conv.test.cpp
	pal/error.test.cpp
	pal/hash.bench.cpp
	pal/hash.test.cpp
	pal/intrusive_queue.test.cpp
	pal/memory.test.cpp
	pal/not_null.test.cpp
	pal/result.test.cpp
	pal/span.test.cpp
)

include(pal/crypto/list.cmake)
include(pal/net/list.cmake)
