list(APPEND pal_sources
	# socket
	pal/net/__socket.hpp
	pal/net/basic_datagram_socket.hpp
	pal/net/basic_socket.hpp
	pal/net/basic_socket_acceptor.hpp
	pal/net/basic_stream_socket.hpp
	pal/net/concepts.hpp
	pal/net/socket_base.hpp
	pal/net/socket_base.posix.cpp
	pal/net/socket_base.windows.cpp
	pal/net/socket_option.hpp

	# internet
	pal/net/ip/address.hpp
	pal/net/ip/address_v4.hpp
	pal/net/ip/address_v4.cpp
	pal/net/ip/address_v6.hpp
	pal/net/ip/address_v6.cpp
	pal/net/ip/basic_endpoint.hpp
	pal/net/ip/basic_endpoint.cpp
	pal/net/ip/host_name.hpp
	pal/net/ip/host_name.cpp
	pal/net/ip/network.hpp
	pal/net/ip/network_v4.hpp
	pal/net/ip/network_v4.cpp
	pal/net/ip/network_v6.hpp
	pal/net/ip/network_v6.cpp
	pal/net/ip/port_type.hpp
	pal/net/ip/socket_option.hpp
	pal/net/ip/tcp.hpp
	pal/net/ip/udp.hpp
)

list(APPEND pal_test_sources
	pal/net/test.hpp

	# socket
	pal/net/basic_socket.test.cpp
	pal/net/basic_socket_acceptor.test.cpp
	pal/net/socket_base.test.cpp
	pal/net/socket_option.test.cpp

	# internet
	pal/net/ip/address.test.cpp
	pal/net/ip/address_v4.test.cpp
	pal/net/ip/address_v6.test.cpp
	pal/net/ip/basic_endpoint.bench.cpp
	pal/net/ip/basic_endpoint.test.cpp
	pal/net/ip/host_name.test.cpp
	pal/net/ip/network.test.cpp
	pal/net/ip/network_v4.test.cpp
	pal/net/ip/network_v6.test.cpp
	pal/net/ip/port_type.test.cpp
	pal/net/ip/tcp.test.cpp
	pal/net/ip/udp.test.cpp
)

list(APPEND pal_fuzz_sources
	pal/net/ip/address_v4.fuzz.cpp
	pal/net/ip/address_v6.fuzz.cpp
	pal/net/ip/network_v4.fuzz.cpp
	pal/net/ip/network_v6.fuzz.cpp
)
