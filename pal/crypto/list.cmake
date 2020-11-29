list(APPEND pal_sources
	pal/crypto/__bits/digest
	pal/crypto/__bits/digest.cpp
	pal/crypto/__bits/x509
	pal/crypto/__bits/x509.cpp
	pal/crypto/certificate
	pal/crypto/certificate.cpp
	pal/crypto/hash
	pal/crypto/hmac
	pal/crypto/key
	pal/crypto/key.cpp
	pal/crypto/oid
	pal/crypto/random
	pal/crypto/random.cpp
)

list(APPEND pal_unittests_sources
	pal/crypto/test
	pal/crypto/certificate.test.cpp
	pal/crypto/hash.test.cpp
	pal/crypto/hmac.test.cpp
	pal/crypto/key.test.cpp
	pal/crypto/oid.test.cpp
	pal/crypto/random.test.cpp
)
