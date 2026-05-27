#include <pal/crypto/certificate.hpp>
#include <pal/crypto/key.hpp>
#include <pal/crypto/test.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators_range.hpp>

namespace
{

using pal::crypto::certificate;
using pal::crypto::key;
using pal::crypto::key_algorithm;
namespace test_cert = pal_test::cert;

TEST_CASE("crypto/key")
{
	SECTION("null")
	{
		const key k{};
		CHECK(k.is_null());
		CHECK_FALSE(k);
	}

	SECTION("public_key")
	{
		const auto *info = GENERATE(from_range(test_cert::data));
		auto cert = certificate::from_pem(info->pem).value();
		auto k = cert.public_key().value();
		REQUIRE(k);
		CHECK(k.algorithm() == key_algorithm::rsa);
		CHECK(k.size_bits() == info->size_bits);
		CHECK(k.max_block_size() == info->max_block_size);
	}
}

} // namespace
