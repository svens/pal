list(APPEND pal_sources
    # socket
    pal/net/__bits/socket
    pal/net/__bits/socket_epoll.ipp
    pal/net/__bits/socket_iocp.ipp
    pal/net/__bits/socket_kqueue.ipp
    pal/net/__bits/socket_posix.cpp
    pal/net/__bits/socket_windows.cpp
    pal/net/basic_datagram_socket
    pal/net/basic_socket
    pal/net/basic_socket_acceptor
    pal/net/basic_stream_socket
    pal/net/socket
    pal/net/socket_base
    pal/net/socket_option

    # internet
    pal/net/ip/__bits/inet
    pal/net/internet
    pal/net/ip/address
    pal/net/ip/address_v4
    pal/net/ip/address_v6
    pal/net/ip/basic_endpoint
    pal/net/ip/basic_resolver
    pal/net/ip/basic_resolver.cpp
    pal/net/ip/socket_option
    pal/net/ip/tcp
    pal/net/ip/udp

    # async
    pal/net/async/request
    pal/net/async/service
)

list(APPEND pal_test_sources
    pal/net/test

    # socket
    pal/net/basic_socket.test.cpp
    pal/net/basic_socket_acceptor.test.cpp
    pal/net/socket_base.test.cpp
    pal/net/socket_option.test.cpp

    # internet
    pal/net/ip/address.test.cpp
    pal/net/ip/address_v4.test.cpp
    pal/net/ip/address_v6.test.cpp
    pal/net/ip/basic_endpoint.test.cpp
    pal/net/ip/basic_resolver.test.cpp
    pal/net/ip/host_name.test.cpp
    pal/net/ip/tcp.test.cpp
    pal/net/ip/udp.test.cpp

    # async
    pal/net/async/request.test.cpp
    pal/net/async/service.test.cpp

    # async/socket
    pal/net/async/basic_datagram_socket.test.cpp
    pal/net/async/basic_socket_acceptor.test.cpp
    pal/net/async/basic_stream_socket.test.cpp
)
