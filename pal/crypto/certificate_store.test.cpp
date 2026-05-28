#include <pal/crypto/certificate_store.hpp>
#include <pal/crypto/test.hpp>
#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <set>
#include <vector>

namespace
{

using pal::crypto::certificate;
using pal::crypto::certificate_store;
namespace test_cert = pal_test::cert;

TEST_CASE("crypto/certificate_store")
{
	SECTION("default")
	{
		certificate_store store;
		CHECK(store.is_null());
		CHECK_FALSE(store);
		CHECK(store.empty());
		CHECK(store.begin() == store.end());
	}

	SECTION("from_pkcs12/memory")
	{
		SECTION("valid")
		{
			auto store = certificate_store::from_pkcs12(test_cert::pkcs12_data);
			REQUIRE(store);
			CHECK_FALSE(store->is_null());
			CHECK(*store);
			CHECK_FALSE(store->empty());
		}

		SECTION("invalid")
		{
			auto store = certificate_store::from_pkcs12(std::string_view{"not-a-pkcs12"});
			REQUIRE_FALSE(store);
			CHECK(store.error() == std::errc::invalid_argument);
		}

		SECTION("certs_only")
		{
			auto store = certificate_store::from_pkcs12(test_cert::pkcs12_ca_data);
			REQUIRE_FALSE(store);
			CHECK(store.error() == std::errc::invalid_argument);
		}
	}

	SECTION("from_pkcs12/file")
	{
		const auto path = std::filesystem::temp_directory_path() / "pal_test.p12";
		std::ofstream{path, std::ios::binary}.write(
			reinterpret_cast<const char *>(std::data(test_cert::pkcs12_data)),
			std::size(test_cert::pkcs12_data)
		);

		auto store = certificate_store::from_pkcs12(path);
		std::filesystem::remove(path);

		REQUIRE(store);
		CHECK_FALSE(store->empty());
		CHECK(store->begin()->fingerprint() == test_cert::server.fingerprint);

		auto not_found = certificate_store::from_pkcs12(path);
		REQUIRE_FALSE(not_found);
		CHECK(not_found.error() == std::errc::no_such_file_or_directory);
	}

	SECTION("iteration")
	{
		auto store = certificate_store::from_pkcs12(test_cert::pkcs12_data).value();

		SECTION("range_for")
		{
			std::vector<std::string_view> fps;
			for (const auto &cert: store)
			{
				fps.push_back(cert.fingerprint());
			}
			REQUIRE(fps.size() == 4);
			CHECK(fps.front() == test_cert::server.fingerprint);

			// clang-format off
			const std::set<std::string_view> rest{fps.begin() + 1, fps.end()};
			CHECK(rest == std::set
			{
				test_cert::client.fingerprint,
				test_cert::intermediate.fingerprint,
				test_cert::ca.fingerprint,
			});
			// clang-format on
		}

		SECTION("post_increment")
		{
			auto it = store.begin();
			auto copy = it++;
			CHECK(copy->fingerprint() == test_cert::server.fingerprint);
			CHECK(it->fingerprint() != test_cert::server.fingerprint);
		}

		SECTION("multipass")
		{
			std::vector<std::string_view> first_pass, second_pass;
			for (const auto &cert: store)
			{
				first_pass.push_back(cert.fingerprint());
			}
			for (const auto &cert: store)
			{
				second_pass.push_back(cert.fingerprint());
			}
			CHECK(first_pass == second_pass);
		}
	}

	SECTION("private_key")
	{
		auto store = certificate_store::from_pkcs12(test_cert::pkcs12_data).value();

		SECTION("leaf_has_key")
		{
			auto it = store.begin();
			REQUIRE(it != store.end());
			CHECK(it->fingerprint() == test_cert::server.fingerprint);

			auto key = it->private_key();
			REQUIRE(key);
			CHECK(key->algorithm() == test_cert::server.algorithm);
			CHECK(key->size_bits() == test_cert::server.size_bits);
			CHECK(key->max_block_size() == test_cert::server.max_block_size);
		}

		SECTION("chain_certs_have_no_key")
		{
			auto it = store.begin();
			REQUIRE(it != store.end());
			++it;
			while (it != store.end())
			{
				auto key = it->private_key();
				CHECK_FALSE(key.has_value());
				CHECK(key.error() == std::errc::io_error);
				++it;
			}
		}
	}

	SECTION("null_private_key")
	{
		auto cert = certificate::from_pem(test_cert::server.pem).value();
		auto key = cert.private_key();
		REQUIRE_FALSE(key);
		CHECK(key.error() == std::errc::io_error);
	}
}

} // namespace
