#include <pal/crypto/certificate_store>
#include <pal/version>
#include <pal/crypto/test>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <set>
#include <vector>

namespace {

using pal::crypto::certificate;
using pal::crypto::certificate_store;

namespace test_cert = pal_test::cert;

TEST_CASE("crypto/certificate_store")
{
	SECTION("default")
	{
		certificate_store store;
		CHECK(store.is_null());
		CHECK(store.empty());
		CHECK(store.begin() == store.end());
	}

	SECTION("from_pkcs12")
	{
		SECTION("unprotected / no password")
		{
			auto store = certificate_store::from_pkcs12(test_cert::pkcs12_unprotected);
			if constexpr (pal::os == pal::os_type::macos)
			{
				// MacOS refuses to import unprotected PKCS#12
				REQUIRE_FALSE(store);
				CHECK(store.error() == std::errc::invalid_argument);
			}
			else
			{
				REQUIRE(store);
				CHECK_FALSE(store.value().empty());
			}
		}

		SECTION("unprotected / with password")
		{
			auto store = certificate_store::from_pkcs12(test_cert::pkcs12_unprotected, test_cert::pkcs12_password);
			REQUIRE_FALSE(store);
			CHECK(store.error() == std::errc::invalid_argument);
		}

		SECTION("protected / no password")
		{
			auto store = certificate_store::from_pkcs12(test_cert::pkcs12_protected);
			REQUIRE_FALSE(store);
			CHECK(store.error() == std::errc::invalid_argument);
		}

		SECTION("protected / with password")
		{
			auto store = certificate_store::from_pkcs12(test_cert::pkcs12_protected, test_cert::pkcs12_password).value();
			REQUIRE(store);
			CHECK_FALSE(store.empty());
		}

		SECTION("protected / invalid password")
		{
			auto store = certificate_store::from_pkcs12(test_cert::pkcs12_protected, "InvalidPassword");
			REQUIRE_FALSE(store);
			CHECK(store.error() == std::errc::invalid_argument);
		}

		SECTION("invalid argument")
		{
			auto store = certificate_store::from_pkcs12("invalid-pkcs12_protected-blob");
			REQUIRE_FALSE(store);
			CHECK(store.error() == std::errc::invalid_argument);
		}
	}

	SECTION("algorithms")
	{
		auto store = certificate_store::from_pkcs12(test_cert::pkcs12_protected, test_cert::pkcs12_password).value();
		REQUIRE_FALSE(store.empty());

		std::vector<certificate> result;

		SECTION("range-for")
		{
			std::set<std::string_view> fingerprints;
			for (auto &cert: store)
			{
				fingerprints.insert(cert.fingerprint());
			}

			static const std::set expected =
			{
				test_cert::server.fingerprint,
				test_cert::client.fingerprint,
				test_cert::intermediate.fingerprint,
				test_cert::ca.fingerprint,
			};

			CHECK(fingerprints == expected);
		}

		SECTION("has_fingerprint")
		{
			std::copy_if(store.begin(), store.end(),
				std::back_inserter(result),
				certificate::has_fingerprint(test_cert::server.fingerprint)
			);
			REQUIRE(result.size() == 1);
			CHECK(result[0].fingerprint() == test_cert::server.fingerprint);
		}

		SECTION("has_common_name")
		{
			std::copy_if(store.begin(), store.end(),
				std::back_inserter(result),
				certificate::has_common_name("pal.alt.ee")
			);
			REQUIRE(result.size() == 2);
			CHECK(result[0].fingerprint() == test_cert::server.fingerprint);
			CHECK(result[1].fingerprint() == test_cert::client.fingerprint);
		}

		SECTION("has_subject_alternative_name")
		{
			std::copy_if(store.begin(), store.end(),
				std::back_inserter(result),
				certificate::has_subject_alternative_name("client.pal.alt.ee")
			);
			REQUIRE(result.size() == 2);
			CHECK(result[0].fingerprint() == test_cert::server.fingerprint);
			CHECK(result[1].fingerprint() == test_cert::client.fingerprint);

			result.clear();
			std::copy_if(store.begin(), store.end(),
				std::back_inserter(result),
				certificate::has_subject_alternative_name("server.pal.alt.ee")
			);
			REQUIRE(result.size() == 1);
			CHECK(result[0].fingerprint() == test_cert::server.fingerprint);
		}
	}
}

} // namespace
