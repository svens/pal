#include <pal/crypto/certificate.hpp>
#include <pal/crypto/test.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <chrono>
#include <format>

namespace
{

namespace test_cert = pal_test::cert;
using namespace std::chrono_literals;

auto load (const test_cert::info &info)
{
	return *pal::crypto::certificate::from_pem(info.pem);
}

TEST_CASE("crypto/certificate")
{
	SECTION("from_pem/valid")
	{
		const auto *info = GENERATE(from_range(test_cert::data));
		auto cert = pal::crypto::certificate::from_pem(info->pem);
		REQUIRE(cert);
		CHECK_FALSE(cert->is_null());
		CHECK(*cert);
	}

	SECTION("from_der/valid")
	{
		const auto *info = GENERATE(from_range(test_cert::data));
		auto cert = pal::crypto::certificate::from_der(test_cert::to_der(info->pem));
		REQUIRE(cert);
		CHECK_FALSE(cert->is_null());
	}

	SECTION("from_pem/invalid")
	{
		CHECK_FALSE(pal::crypto::certificate::from_pem(""));
		CHECK_FALSE(pal::crypto::certificate::from_pem("garbage"));
		CHECK_FALSE(pal::crypto::certificate::from_pem("-----BEGIN CERTIFICATE-----\n"));
		CHECK_FALSE(
			pal::crypto::certificate::from_pem(
				"-----BEGIN CERTIFICATE-----\n"
				"!!!!\n"
				"-----END CERTIFICATE-----\n"
			)
		);
	}

	SECTION("from_der/invalid")
	{
		CHECK_FALSE(pal::crypto::certificate::from_der(std::string{}));
		CHECK_FALSE(pal::crypto::certificate::from_der(std::string{"garbage"}));
	}

	SECTION("as_bytes")
	{
		const auto *info = GENERATE(from_range(test_cert::data));
		const auto cert = load(*info);
		const auto bytes = cert.as_bytes();
		const auto der = test_cert::to_der(info->pem);
		CHECK(std::string_view{reinterpret_cast<const char *>(bytes.data()), bytes.size()} == der);
	}

	SECTION("version")
	{
		const auto *info = GENERATE(from_range(test_cert::data));
		CHECK(load(*info).version() == info->version);
	}

	SECTION("serial_number")
	{
		const auto *info = GENERATE(from_range(test_cert::data));
		const auto cert = load(*info);
		CHECK(pal_test::to_hex(cert.serial_number()) == info->serial_number);
	}

	SECTION("common_name")
	{
		const auto *info = GENERATE(from_range(test_cert::data));
		CHECK(load(*info).common_name() == info->common_name);
	}

	SECTION("fingerprint")
	{
		const auto *info = GENERATE(from_range(test_cert::data));
		CHECK(load(*info).fingerprint() == info->fingerprint);
	}

	SECTION("validity")
	{
		const auto *info = GENERATE(from_range(test_cert::data));
		const auto cert = load(*info);
		CHECK(cert.not_before() < cert.not_after());
		CHECK(cert.not_expired_at(cert.not_before()));
		CHECK(cert.not_expired_at(cert.not_after()));
		CHECK_FALSE(cert.not_expired_at(cert.not_before() - 1s));
		CHECK_FALSE(cert.not_expired_at(cert.not_after() + 1s));
		CHECK(cert.not_expired_for(0s, cert.not_before()));
		CHECK_FALSE(cert.not_expired_for(1s, cert.not_after()));
	}

	SECTION("subject_name")
	{
		const auto *info = GENERATE(from_range(test_cert::data));
		CAPTURE(info->fingerprint);
		CHECK(std::format("{}", load(*info).subject_name()) == info->subject_name);
	}

	SECTION("issuer_name")
	{
		const auto *info = GENERATE(from_range(test_cert::data));
		CAPTURE(info->fingerprint);
		CHECK(std::format("{}", load(*info).issuer_name()) == info->issuer_name);
	}

	SECTION("subject_alternative_name")
	{
		const auto *info = GENERATE(from_range(test_cert::data));
		CAPTURE(info->fingerprint);
		auto an = load(*info).subject_alternative_name().value();
		CHECK(std::format("{}", an) == info->subject_alternative_name);
	}

	SECTION("issuer_alternative_name")
	{
		const auto *info = GENERATE(from_range(test_cert::data));
		CAPTURE(info->fingerprint);
		auto an = load(*info).issuer_alternative_name().value();
		CHECK(std::format("{}", an) == info->issuer_alternative_name);
	}

	SECTION("subject_alternative_name_value")
	{
		{
			const auto san = load(test_cert::server).subject_alternative_name_value();

			CHECK(san.contains("server.pal.alt.ee"));
			CHECK(san.contains("anything.pal.alt.ee")); // wildcard *.pal.alt.ee
			CHECK(san.contains("1.2.3.4"));
			CHECK(san.contains("2001:db8:85a3::8a2e:370:7334"));

			CHECK_FALSE(san.contains("pal.alt.ee"));
			CHECK_FALSE(san.contains("sub.sub.pal.alt.ee"));
			CHECK_FALSE(san.contains("*.pal.alt.ee"));
			CHECK_FALSE(san.contains(".pal.alt.ee"));
			CHECK_FALSE(san.contains("pal@alt.ee"));
			CHECK_FALSE(san.contains("https://pal.alt.ee/path")); // URI not stored
		}

		{
			const auto san = load(test_cert::client).subject_alternative_name_value();

			CHECK(san.contains("client.pal.alt.ee"));
			CHECK_FALSE(san.contains("server.pal.alt.ee"));
			CHECK_FALSE(san.contains("pal@alt.ee")); // email not stored
		}

		{
			const auto san = load(test_cert::self_signed).subject_alternative_name_value();
			CHECK_FALSE(san.contains("anything"));
		}
	}

	SECTION("is_issued_by")
	{
		const auto ca = load(test_cert::ca);
		const auto intermediate = load(test_cert::intermediate);
		const auto server = load(test_cert::server);
		const auto self_signed = load(test_cert::self_signed);

		CHECK(ca.is_self_signed());
		CHECK(intermediate.is_issued_by(ca));
		CHECK(server.is_issued_by(intermediate));
		CHECK(self_signed.is_self_signed());

		CHECK_FALSE(server.is_self_signed());
		CHECK_FALSE(server.is_issued_by(ca));
		CHECK_FALSE(ca.is_issued_by(intermediate));
	}
}

} // namespace
