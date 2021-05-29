list(APPEND pal_sources
    # internal
    pal/net/__bits/socket
    pal/net/__bits/socket_posix.cpp
    pal/net/__bits/socket_windows.cpp
    pal/net/ip/__bits/inet

    # socket
    pal/net/basic_datagram_socket
    pal/net/basic_socket
    pal/net/basic_stream_socket
    pal/net/socket
    pal/net/socket_base
    pal/net/socket_option

    # internet
    pal/net/internet
    pal/net/ip/tcp
    pal/net/ip/udp
)

list(APPEND pal_test_sources
    pal/net/test
    pal/net/basic_socket.test.cpp
    pal/net/socket_base.test.cpp
    pal/net/socket_option.test.cpp
)
