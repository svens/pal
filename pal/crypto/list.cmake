list(APPEND pal_sources
	pal/crypto/digest_algorithm
	pal/crypto/digest_algorithm_linux.cpp
	pal/crypto/digest_algorithm_macos.cpp
	pal/crypto/digest_algorithm_windows.cpp
	pal/crypto/hash
	pal/crypto/hmac
	pal/crypto/oid
	pal/crypto/random
	pal/crypto/random.cpp
)

list(APPEND pal_test_sources
	pal/crypto/test
	pal/crypto/hash.test.cpp
	pal/crypto/hmac.test.cpp
	pal/crypto/oid.test.cpp
	pal/crypto/random.test.cpp
)
