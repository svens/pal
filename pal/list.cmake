list(APPEND pal_sources
	pal/__bits/lib
	pal/__bits/platform_sdk
	pal/cancel_token
	pal/error
	pal/error.cpp
	pal/expect
	pal/intrusive_mpsc_queue
	pal/intrusive_queue
	pal/intrusive_stack
	pal/span
	pal/task
	pal/worker
)

list(APPEND pal_unittests_sources
	pal/test
	pal/test.cpp
	pal/cancel_token.test.cpp
	pal/error.test.cpp
	pal/expect.test.cpp
	pal/intrusive_mpsc_queue.test.cpp
	pal/intrusive_queue.test.cpp
	pal/intrusive_stack.test.cpp
	pal/span.test.cpp
	pal/task.test.cpp
	pal/worker.test.cpp
)
