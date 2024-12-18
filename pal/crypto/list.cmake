list(APPEND pal_sources
	pal/crypto/alternative_name
	pal/crypto/alternative_name_value
	pal/crypto/certificate
	pal/crypto/certificate.cpp
	pal/crypto/certificate_linux.ipp
	pal/crypto/certificate_macos.ipp
	pal/crypto/certificate_windows.ipp
	pal/crypto/certificate_store
	pal/crypto/digest_algorithm
	pal/crypto/digest_algorithm.cpp
	pal/crypto/digest_algorithm_linux.ipp
	pal/crypto/digest_algorithm_macos.ipp
	pal/crypto/digest_algorithm_windows.ipp
	pal/crypto/distinguished_name
	pal/crypto/hash
	pal/crypto/hmac
	pal/crypto/key
	pal/crypto/oid
	pal/crypto/random
	pal/crypto/random.cpp
	pal/crypto/sign
)

list(APPEND pal_test_sources
	pal/crypto/test
	pal/crypto/test.pkcs12_protected.ipp
	pal/crypto/test.pkcs12_unprotected.ipp
	pal/crypto/certificate.test.cpp
	pal/crypto/certificate_store.test.cpp
	pal/crypto/hash.test.cpp
	pal/crypto/hmac.test.cpp
	pal/crypto/key.test.cpp
	pal/crypto/oid.test.cpp
	pal/crypto/random.test.cpp
	pal/crypto/sign.test.cpp
)
