list(APPEND pal_sources
	pal/buffer.hpp
	pal/byte_order.hpp
	pal/codec.hpp
	pal/codec_base64.cpp
	pal/codec_hex.cpp
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
	pal/buffer.test.cpp
	pal/byte_order.test.cpp
	pal/codec.bench.cpp
	pal/codec.test.cpp
	pal/error.test.cpp
	pal/hash.bench.cpp
	pal/hash.test.cpp
	pal/intrusive_queue.test.cpp
	pal/intrusive_stack.test.cpp
	pal/result.test.cpp
	pal/span.test.cpp
)

list(APPEND pal_fuzz_sources
	pal/fuzz.hpp
)

include(pal/net/list.cmake)
