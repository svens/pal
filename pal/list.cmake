list(APPEND pal_sources
	pal/__bits/lib
	pal/__bits/platform_sdk
	pal/assert
	pal/error
	pal/error.cpp
)

list(APPEND pal_unittests_sources
	pal/test
	pal/test.cpp
	pal/assert.test.cpp
	pal/error.test.cpp
)
