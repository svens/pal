#include <pal/crypto/certificate_store>
#include <pal/version>
#include <pal/crypto/test>
#include <catch2/catch_test_macros.hpp>

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
				auto it = store.value().begin(), end = store.value().end();
				CHECK(it != end);
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
			auto it = store.begin(), end = store.end();
			CHECK(it != end);
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
		auto it = store.begin(), end = store.end();

		REQUIRE(it != end);
		CHECK(it->fingerprint() == test_cert::server.fingerprint);

		it++;
		REQUIRE(it != end);
		CHECK(it->fingerprint() == test_cert::intermediate.fingerprint);

		it++;
		REQUIRE(it != end);
		CHECK(it->fingerprint() == test_cert::ca.fingerprint);

		it++;
		CHECK(it == end);
	}
}

} // namespace
