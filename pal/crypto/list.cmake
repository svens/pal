list(APPEND pal_sources
	pal/crypto/oid
	pal/crypto/random
	pal/crypto/random.cpp
)

list(APPEND pal_test_sources
	pal/crypto/test
	pal/crypto/oid.test.cpp
	pal/crypto/random.test.cpp
)
