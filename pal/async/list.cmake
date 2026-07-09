list(APPEND pal_sources
	pal/async/__async.hpp
	pal/async/event_loop.hpp
	pal/async/event_loop.cpp
	pal/async/event_loop.select.cpp
	pal/async/task.hpp
)

list(APPEND pal_test_sources
	pal/async/__async.test.cpp
	pal/async/task.test.cpp
	pal/async/event_loop.test.cpp
)
