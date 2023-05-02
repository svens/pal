list(APPEND pal_sources
	pal/crypto/__algorithm
	pal/crypto/__algorithm_linux.cpp
	pal/crypto/__algorithm_macos.cpp
	pal/crypto/__algorithm_windows.cpp
	pal/crypto/hash
	pal/crypto/oid
	pal/crypto/random
	pal/crypto/random.cpp
)

list(APPEND pal_test_sources
	pal/crypto/test
	pal/crypto/hash.test.cpp
	pal/crypto/oid.test.cpp
	pal/crypto/random.test.cpp
)
