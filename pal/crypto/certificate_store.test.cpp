#include <pal/crypto/certificate_store>
#include <pal/version>
#include <pal/crypto/test>
#include <catch2/catch_test_macros.hpp>
#include <set>

namespace {

using pal::crypto::certificate_store;

namespace test_cert = pal_test::cert;

TEST_CASE("crypto/certificate_store")
{
	SECTION("default")
	{
		certificate_store store;
		CHECK(store.is_null());
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

	SECTION("loop")
	{
		auto store = certificate_store::from_pkcs12(test_cert::pkcs12_protected, test_cert::pkcs12_password).value();

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
}

} // namespace
