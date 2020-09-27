list(APPEND pal_sources
	pal/net/__bits/socket
	pal/net/__bits/socket.cpp
	pal/net/error
	pal/net/error.cpp
	pal/net/socket_base

	pal/net/ip/__bits/inet
	pal/net/ip/address_v4
)

list(APPEND pal_unittests_sources
	pal/net/test
	pal/net/init.test.cpp
	pal/net/error.test.cpp
	pal/net/ip/address_v4.test.cpp
)
