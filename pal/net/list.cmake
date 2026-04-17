list(APPEND pal_sources
	pal/net/__socket.hpp
	pal/net/socket_base.hpp
	pal/net/socket_base.posix.cpp
	pal/net/socket_base.windows.cpp
)

list(APPEND pal_test_sources
	pal/net/socket_base.test.cpp
)
