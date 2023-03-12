list(APPEND pal_sources
	pal/__config
	pal/__expected
	pal/byte_order
	pal/error
	pal/error.cpp
	pal/result
)

list(APPEND pal_test_sources
	pal/test
	pal/test.cpp
	pal/byte_order.test.cpp
	pal/error.test.cpp
	pal/result.test.cpp
)
