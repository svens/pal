list(APPEND pal_sources
	pal/crypto/__bits/digest
	pal/crypto/hash
	pal/crypto/hash.cpp

	pal/crypto/random
	pal/crypto/random.cpp
)

list(APPEND pal_unittests_sources
	pal/crypto/test
	pal/crypto/hash.test.cpp
	pal/crypto/random.test.cpp
)
