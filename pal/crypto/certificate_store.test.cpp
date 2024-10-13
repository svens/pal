#include <pal/crypto/certificate_store>
#include <pal/crypto/test>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <set>
#include <vector>

// In tests below, MacOS has special cases, see pal::crypto::certificate_store::from_pkcs12()

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
			auto store = certificate_store::from_pkcs12(test_cert::pkcs12_unprotected).value();
			REQUIRE(store);
			CHECK_FALSE(store.empty());
		}

		SECTION("protected / with password")
		{
			auto store = certificate_store::from_pkcs12(test_cert::pkcs12_protected, test_cert::pkcs12_password).value();
			REQUIRE(store);
			CHECK_FALSE(store.empty());
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

		SECTION("private_key")
		{
			auto store = certificate_store::from_pkcs12(test_cert::pkcs12_protected, test_cert::pkcs12_password).value();
			REQUIRE_FALSE(store.empty());

			auto it = store.begin();
			REQUIRE(it != store.end());
			auto key = it->private_key();
			REQUIRE(key.has_value());
			CHECK(it->fingerprint() == test_cert::server.fingerprint);
			CHECK(key->max_block_size() == test_cert::server.max_block_size);
			CHECK(key->size_bits() == test_cert::server.size_bits);

			REQUIRE(++it != store.end());
			key = it->private_key();
			REQUIRE_FALSE(key.has_value());
			CHECK(key.error() == std::errc::io_error);
			CHECK(it->fingerprint() != test_cert::server.fingerprint);
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

			auto expected = []() -> std::set<std::string_view>
			{
				if constexpr (pal::os != pal::os_type::macos)
				{
					return
					{
						test_cert::server.fingerprint,
						test_cert::client.fingerprint,
						test_cert::intermediate.fingerprint,
						test_cert::ca.fingerprint,
					};
				}
				else
				{
					return
					{
						test_cert::server.fingerprint,
						test_cert::intermediate.fingerprint,
						test_cert::ca.fingerprint,
					};
				}
			};


			CHECK(fingerprints == expected());
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

			if constexpr (pal::os != pal::os_type::macos)
			{
				REQUIRE(result.size() == 2);
				CHECK(result[0].fingerprint() == test_cert::server.fingerprint);
				CHECK(result[1].fingerprint() == test_cert::client.fingerprint);
			}
			else
			{
				REQUIRE(result.size() == 1);
				CHECK(result[0].fingerprint() == test_cert::server.fingerprint);
			}
		}

		SECTION("has_subject_alternative_name")
		{
			// server cert will be returned because it has DNS entry *.pal.alt.ee
			std::copy_if(store.begin(), store.end(),
				std::back_inserter(result),
				certificate::has_subject_alternative_name("client.pal.alt.ee")
			);

			if constexpr (pal::os != pal::os_type::macos)
			{
				REQUIRE(result.size() == 2);
				CHECK(result[0].fingerprint() == test_cert::server.fingerprint);
				CHECK(result[1].fingerprint() == test_cert::client.fingerprint);
			}
			else
			{
				REQUIRE(result.size() == 1);
				CHECK(result[0].fingerprint() == test_cert::server.fingerprint);
			}

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
