list(APPEND pal_sources
	# internet
	pal/net/ip/address
	pal/net/ip/address_v4
	pal/net/ip/address_v4.cpp
	pal/net/ip/address_v6
	pal/net/ip/address_v6.cpp
)

list(APPEND pal_test_sources
	pal/net/test

	# internet
	pal/net/ip/address.test.cpp
	pal/net/ip/address_v4.test.cpp
	pal/net/ip/address_v6.test.cpp
)
