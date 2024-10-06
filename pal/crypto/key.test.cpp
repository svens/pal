#include <pal/crypto/key>
#include <pal/crypto/certificate_store>
#include <pal/crypto/test>
#include <catch2/catch_test_macros.hpp>

namespace {

using pal::crypto::certificate_store;
using pal::crypto::key_algorithm;

namespace test_cert = pal_test::cert;

TEST_CASE("crypto/key")
{
	auto store = certificate_store::from_pkcs12(test_cert::pkcs12_unprotected).value();
	REQUIRE_FALSE(store.empty());

	auto certificate = *store.begin();
	REQUIRE(certificate);
	REQUIRE(certificate.fingerprint() == test_cert::server.fingerprint);

	SECTION("public_key")
	{
		auto key = certificate.public_key().value();
		REQUIRE(key);
		CHECK(key.algorithm() == key_algorithm::rsa);
		CHECK(key.max_block_size() == test_cert::server.max_block_size);
		CHECK(key.size_bits() == test_cert::server.size_bits);
	}

	SECTION("private_key")
	{
		auto key = certificate.private_key().value();
		REQUIRE(key);
		CHECK(key.algorithm() == key_algorithm::rsa);
		CHECK(key.max_block_size() == test_cert::server.max_block_size);
		CHECK(key.size_bits() == test_cert::server.size_bits);
	}
}

} // namespace
