#include <pal/crypto/certificate.hpp>
#include <pal/crypto/test.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators_range.hpp>

namespace
{

namespace test_cert = pal_test::cert;

TEST_CASE("crypto/certificate")
{
	SECTION("from_pem/valid")
	{
		const auto *info = GENERATE(from_range(std::begin(test_cert::data), std::end(test_cert::data)));
		auto cert = pal::crypto::certificate::from_pem(info->pem);
		REQUIRE(cert);
		CHECK_FALSE(cert->is_null());
	}

	SECTION("from_der/valid")
	{
		const auto *info = GENERATE(from_range(std::begin(test_cert::data), std::end(test_cert::data)));
		auto cert = pal::crypto::certificate::from_der(test_cert::to_der(info->pem));
		REQUIRE(cert);
		CHECK_FALSE(cert->is_null());
	}

	SECTION("from_pem/invalid")
	{
		CHECK_FALSE(pal::crypto::certificate::from_pem(""));
		CHECK_FALSE(pal::crypto::certificate::from_pem("garbage"));
		CHECK_FALSE(pal::crypto::certificate::from_pem("-----BEGIN CERTIFICATE-----\n"));
	}

	SECTION("from_der/invalid")
	{
		CHECK_FALSE(pal::crypto::certificate::from_der(std::string{}));
		CHECK_FALSE(pal::crypto::certificate::from_der(std::string{"garbage"}));
	}
}

} // namespace
