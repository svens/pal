list(APPEND pal_sources
	pal/crypto/__crypto.hpp
	pal/crypto/random.hpp
	pal/crypto/random.openssl.cpp
	pal/crypto/random.windows.cpp
)

list(APPEND pal_test_sources
	pal/crypto/random.test.cpp
)
