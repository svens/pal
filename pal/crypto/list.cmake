list(APPEND pal_sources
	pal/crypto/__crypto.hpp
	pal/crypto/random.hpp
	pal/crypto/random.windows.cpp
	pal/crypto/random.openssl.cpp
)

list(APPEND pal_test_sources
	pal/crypto/random.test.cpp
)
