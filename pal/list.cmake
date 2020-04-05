list(APPEND pal_sources
	pal/__bits/lib
	pal/__bits/platform_sdk
	pal/assert
	pal/cancel_token
	pal/error
	pal/error.cpp
	pal/intrusive_mpsc_queue
	pal/intrusive_queue
	pal/intrusive_stack
	pal/span
)

list(APPEND pal_unittests_sources
	pal/test
	pal/test.cpp
	pal/assert.test.cpp
	pal/cancel_token.test.cpp
	pal/error.test.cpp
	pal/intrusive_mpsc_queue.test.cpp
	pal/intrusive_queue.test.cpp
	pal/intrusive_stack.test.cpp
	pal/span.test.cpp
)
