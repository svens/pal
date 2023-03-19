list(APPEND pal_sources
	# internet
	pal/net/ip/address_v4
	pal/net/ip/address_v4.cpp
)

list(APPEND pal_test_sources
	pal/net/test

	# internet
	pal/net/ip/address_v4.test.cpp
)
