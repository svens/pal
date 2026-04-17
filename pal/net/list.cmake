list(APPEND pal_sources
	pal/net/__socket.hpp
	pal/net/socket_base.hpp
	pal/net/socket_base.posix.cpp
	pal/net/socket_base.windows.cpp
	pal/net/socket_option.hpp
)

list(APPEND pal_test_sources
	pal/net/socket_base.test.cpp
	pal/net/socket_option.test.cpp
)
