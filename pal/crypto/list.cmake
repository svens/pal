list(APPEND pal_sources
	pal/crypto/__certificate.hpp
	pal/crypto/__crypto.hpp
	pal/crypto/certificate.hpp
	pal/crypto/certificate.cpp
	pal/crypto/certificate.openssl.cpp
	pal/crypto/certificate.windows.cpp
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
	pal/crypto/certificate.test.cpp
	pal/crypto/hash.test.cpp
	pal/crypto/hmac.test.cpp
	pal/crypto/oid.test.cpp
	pal/crypto/random.test.cpp
	pal/crypto/test.hpp
	pal/crypto/test_certs.hpp
	pal/crypto/test_pkcs12.hpp
)
