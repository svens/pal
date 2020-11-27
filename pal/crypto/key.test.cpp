#include <pal/crypto/key>
#include <pal/crypto/certificate>
#include <pal/crypto/test>


namespace {


TEST_CASE("crypto/key")
{
	using namespace pal::crypto;
	namespace test_cert = pal_test::cert;

	SECTION("public_key")
	{
		SECTION("default ctor")
		{
			public_key null;
			CHECK(null.is_null());
			CHECK(!null);
			CHECK(null.size_bits() == 0);
			CHECK(null.algorithm() == key_algorithm::opaque);
		}

		SECTION("swap")
		{
			public_key a;
			CHECK(a.is_null());
			CHECK(a.size_bits() == 0);
			CHECK(a.algorithm() == key_algorithm::opaque);

			public_key b = certificate::from_pem(test_cert::ca_pem).public_key();
			CHECK_FALSE(b.is_null());
			CHECK(b.size_bits() == 4096);
			CHECK(b.algorithm() == key_algorithm::rsa);

			a.swap(b);

			CHECK_FALSE(a.is_null());
			CHECK(a.size_bits() == 4096);
			CHECK(a.algorithm() == key_algorithm::rsa);

			CHECK(b.is_null());
			CHECK(b.size_bits() == 0);
			CHECK(b.algorithm() == key_algorithm::opaque);
		}

		SECTION("properties")
		{
			auto [pem, algorithm, size_bits] = GENERATE(
				table<std::string_view, key_algorithm, size_t>({
					{ test_cert::ca_pem, key_algorithm::rsa, 4096 },
					{ test_cert::intermediate_pem, key_algorithm::rsa, 4096 },
					{ test_cert::server_pem, key_algorithm::rsa, 2048 },
					{ test_cert::client_pem, key_algorithm::rsa, 2048 },
				})
			);
			auto cert = certificate::from_pem(pem);
			auto key = cert.public_key();
			REQUIRE(key);
			CHECK(key.algorithm() == algorithm);
			CHECK(key.size_bits() == size_bits);
		}
	}

	SECTION("private_key")
	{
		// null
		private_key null;
		CHECK(null.is_null());
		CHECK(!null);
		CHECK(null.size_bits() == 0);
		CHECK(null.algorithm() == key_algorithm::opaque);

		// properties
		private_key key;
		auto pkcs12 = pal_test::to_der(test_cert::pkcs12);
		certificate::import_pkcs12(std::span{pkcs12}, "TestPassword", &key);
		REQUIRE_FALSE(key.is_null());
		CHECK(key.algorithm() == key_algorithm::rsa);
		CHECK(key.size_bits() == 2048);

		// swap
		key.swap(null);
		CHECK_FALSE(null.is_null());
		CHECK(null.size_bits() == 2048);
		CHECK(null.algorithm() == key_algorithm::rsa);
		CHECK(key.is_null());
		CHECK(key.size_bits() == 0);
		CHECK(key.algorithm() == key_algorithm::opaque);
	}
}


} // namespace
