list(APPEND pal_sources
	pal/async/__async.hpp
	pal/async/datagram_socket.hpp
	pal/async/event_loop.hpp
	pal/async/event_loop.cpp
	pal/async/event_loop.epoll.cpp
	pal/async/event_loop.iocp.cpp
	pal/async/event_loop.kqueue.cpp
	pal/async/handle.hpp
	pal/async/resolver.hpp
	pal/async/task.hpp
	pal/async/task_pool.hpp
	pal/async/thread_pool.hpp
	pal/async/thread_pool.cpp
)

list(APPEND pal_test_sources
	pal/async/__async.test.cpp
	pal/async/datagram_socket.test.cpp
	pal/async/event_loop.test.cpp
	pal/async/resolver.test.cpp
	pal/async/task.test.cpp
	pal/async/task_pool.test.cpp
	pal/async/thread_pool.test.cpp
)
