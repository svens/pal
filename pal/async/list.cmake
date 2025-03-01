list(APPEND pal_sources
	pal/async/__impl
	pal/async/__impl.epoll.cpp
	pal/async/__impl.iocp.cpp
	pal/async/__impl.kqueue.cpp
	pal/async/datagram_socket
	pal/async/event_loop
	pal/async/event_loop.cpp
	pal/async/fwd
	pal/async/task
)

list(APPEND pal_test_sources
	pal/async/event_loop.test.cpp
	pal/async/datagram_socket.test.cpp
)
