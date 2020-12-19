list(APPEND pal_sources
	pal/__bits/lib
	pal/__bits/platform_sdk
	pal/__bits/ref
	pal/assert
	pal/byte_order
	pal/conv
	pal/conv_base64.cpp
	pal/conv_hex.cpp
	pal/error
	pal/error.cpp
	pal/hash
	pal/intrusive_mpsc_queue
	pal/intrusive_queue
	pal/intrusive_stack
	pal/not_null
	pal/scoped_alloc
	pal/span
	pal/string
)

list(APPEND pal_unittests_sources
	pal/test
	pal/test.cpp
	pal/assert.test.cpp
	pal/byte_order.test.cpp
	pal/conv.test.cpp
	pal/error.test.cpp
	pal/hash.test.cpp
	pal/intrusive_mpsc_queue.test.cpp
	pal/intrusive_queue.test.cpp
	pal/intrusive_stack.test.cpp
	pal/not_null.test.cpp
	pal/scoped_alloc.test.cpp
	pal/span.test.cpp
)

include(pal/crypto/list.cmake)
include(pal/net/list.cmake)
