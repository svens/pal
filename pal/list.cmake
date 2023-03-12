list(APPEND pal_sources
	pal/__config
	pal/byte_order
	pal/error
	pal/error.cpp
)

list(APPEND pal_test_sources
	pal/test
	pal/test.cpp
	pal/byte_order.test.cpp
	pal/error.test.cpp
)
