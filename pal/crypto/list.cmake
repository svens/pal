list(APPEND pal_sources
	pal/crypto/__crypto.hpp
	pal/crypto/concepts.hpp
	pal/crypto/digest_algorithm.hpp
	pal/crypto/digest_algorithm.openssl.cpp
	pal/crypto/digest_algorithm.windows.cpp
	pal/crypto/hash.hpp
	pal/crypto/hmac.hpp
	pal/crypto/oid.hpp
	pal/crypto/random.hpp
	pal/crypto/random.openssl.cpp
	pal/crypto/random.windows.cpp
)

list(APPEND pal_test_sources
	pal/crypto/hash.test.cpp
	pal/crypto/hmac.test.cpp
	pal/crypto/oid.test.cpp
	pal/crypto/random.test.cpp
)
