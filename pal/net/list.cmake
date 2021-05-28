list(APPEND pal_sources
    # internal
    pal/net/__bits/socket
    pal/net/__bits/socket_posix.cpp
    pal/net/__bits/socket_windows.cpp

    # socket
    pal/net/socket
    pal/net/socket_base

    # internet
    pal/net/ip/__bits/inet
)

list(APPEND pal_test_sources
    pal/net/test
    pal/net/init.test.cpp
)
