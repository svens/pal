list(APPEND pal_sources
	pal/__platform_sdk.hpp
	pal/byte_order.hpp
	pal/error.hpp
	pal/error.cpp
	pal/hash.hpp
	pal/intrusive_queue.hpp
	pal/intrusive_stack.hpp
	pal/masked_formatter.hpp
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
	pal/masked_formatter.test.cpp
	pal/result.test.cpp
	pal/span.test.cpp
)

include(pal/net/list.cmake)
