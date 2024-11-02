list(APPEND pal_sources
	# socket
	pal/net/__socket
	pal/net/socket
	pal/net/socket.posix.cpp
	pal/net/socket.windows.cpp
	pal/net/socket_base

	# internet
	pal/net/ip/address
	pal/net/ip/address_v4
	pal/net/ip/address_v4.cpp
	pal/net/ip/address_v6
	pal/net/ip/address_v6.cpp
)

list(APPEND pal_test_sources
	pal/net/test

	# socket
	pal/net/socket.test.cpp

	# internet
	pal/net/ip/address.test.cpp
	pal/net/ip/address_v4.test.cpp
	pal/net/ip/address_v6.test.cpp
)
