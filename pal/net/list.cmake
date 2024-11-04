list(APPEND pal_sources
	# socket
	pal/net/__socket
	pal/net/socket
	pal/net/socket_base
	pal/net/socket_base.posix.cpp
	pal/net/socket_base.windows.cpp

	# internet
	pal/net/ip/address
	pal/net/ip/address_v4
	pal/net/ip/address_v4.cpp
	pal/net/ip/address_v6
	pal/net/ip/address_v6.cpp
	pal/net/ip/tcp
	pal/net/ip/udp
)

list(APPEND pal_test_sources
	pal/net/test

	# socket
	pal/net/socket_base.test.cpp

	# internet
	pal/net/ip/address.test.cpp
	pal/net/ip/address_v4.test.cpp
	pal/net/ip/address_v6.test.cpp
	pal/net/ip/tcp.test.cpp
	pal/net/ip/udp.test.cpp
)
