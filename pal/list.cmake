list(APPEND pal_sources
	pal/__diagnostic.hpp
	pal/buffer.hpp
	pal/byte_order.hpp
	pal/codec.hpp
	pal/codec_base64.cpp
	pal/codec_hex.cpp
	pal/error.hpp
	pal/error.cpp
	pal/hash.hpp
	pal/intrusive_mpsc_queue.hpp
	pal/intrusive_mpsc_stack.hpp
	pal/intrusive_queue.hpp
	pal/intrusive_stack.hpp
	pal/memory.hpp
	pal/require.hpp
	pal/require.cpp
	pal/result.hpp
	pal/spsc_bounded_queue.hpp
	pal/version.hpp
)

list(APPEND pal_test_sources
	pal/test.hpp
	pal/test.cpp
	pal/__diagnostic.test.cpp
	pal/buffer.test.cpp
	pal/byte_order.test.cpp
	pal/codec.bench.cpp
	pal/codec.test.cpp
	pal/error.test.cpp
	pal/hash.bench.cpp
	pal/hash.test.cpp
	pal/intrusive_mpsc_queue.bench.cpp
	pal/intrusive_mpsc_queue.test.cpp
	pal/intrusive_mpsc_stack.test.cpp
	pal/intrusive_queue.test.cpp
	pal/intrusive_stack.test.cpp
	pal/memory.test.cpp
	pal/require.test.cpp
	pal/result.test.cpp
	pal/spsc_bounded_queue.test.cpp
)

list(APPEND pal_fuzz_sources
	pal/fuzz.hpp
	pal/codec_base64.fuzz.cpp
	pal/codec_hex.fuzz.cpp
)

include(pal/crypto/list.cmake)
include(pal/net/list.cmake)
