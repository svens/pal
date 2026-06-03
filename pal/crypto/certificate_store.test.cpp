#include <pal/crypto/certificate_store.hpp>
#include <pal/crypto/__crypto.hpp>
#include <pal/crypto/test.hpp>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
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

	SECTION("from_pkcs12/not_enough_memory")
	{
		const auto path = std::filesystem::temp_directory_path() / "pal_test_oom.p12";
		std::ofstream{path, std::ios::binary}.write(
			reinterpret_cast<const char *>(std::data(test_cert::pkcs12_data)),
			std::size(test_cert::pkcs12_data)
		);

		const pal_test::bad_alloc_once x;
		auto store = certificate_store::from_pkcs12(path);
		std::filesystem::remove(path);

		REQUIRE_FALSE(store);
		CHECK(store.error() == std::errc::not_enough_memory);
	}

	SECTION("iteration")
	{
		auto store = certificate_store::from_pkcs12(test_cert::pkcs12_data).value();

		SECTION("range_for")
		{
			std::vector<std::string_view> fps;
			std::ranges::transform(store, std::back_inserter(fps), &certificate::fingerprint);
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
			std::ranges::transform(store, std::back_inserter(first_pass), &certificate::fingerprint);
			std::ranges::transform(store, std::back_inserter(second_pass), &certificate::fingerprint);
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

namespace
{

struct scoped_dir
{
	std::filesystem::path path;

	explicit scoped_dir (std::string_view name)
		: path{std::filesystem::temp_directory_path() / name}
	{
		std::filesystem::remove_all(path);
		std::filesystem::create_directories(path);
	}

	~scoped_dir () noexcept
	{
		std::error_code ec;
		std::filesystem::remove_all(path, ec);
	}

	void write_file (std::string_view name, std::span<const std::byte> data) const
	{
		std::ofstream{path / std::string{name}, std::ios::binary}.write(
			reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size())
		);
	}

	void write_pkcs12 (std::string_view name) const
	{
		write_file(name, std::as_bytes(std::span{test_cert::pkcs12_data}));
	}
};

void set_env (const char *name, const char *value) noexcept
{
#ifdef _WIN32
	_putenv_s(name, value);
#else
	::setenv(name, value, 1);
#endif
}

void unset_env (const char *name) noexcept
{
#ifdef _WIN32
	_putenv_s(name, "");
#else
	::unsetenv(name);
#endif
}

} // namespace

TEST_CASE("crypto/certificate_store/from_cert_dir")
{
	SECTION("nonexistent")
	{
		const auto missing = std::filesystem::temp_directory_path() / "pal_cert_dir_missing_xyz";
		std::filesystem::remove_all(missing);

		auto store = certificate_store::from_cert_dir(missing);
		REQUIRE_FALSE(store);
	}

	SECTION("empty")
	{
		const scoped_dir dir{"pal_cert_dir_empty"};
		auto store = certificate_store::from_cert_dir(dir.path);
		REQUIRE(store);
		CHECK_FALSE(store->is_null());
		CHECK(*store);
		CHECK(store->empty());
		CHECK(store->begin() == store->end());
	}

	SECTION("single_pfx")
	{
		const scoped_dir dir{"pal_cert_dir_single"};
		dir.write_pkcs12("identity.pfx");

		auto store = certificate_store::from_cert_dir(dir.path).value();
		CHECK_FALSE(store.empty());

		std::vector<std::string_view> fps;
		std::ranges::transform(store, std::back_inserter(fps), &certificate::fingerprint);
		REQUIRE(fps.size() == 4);
		CHECK(fps.front() == test_cert::server.fingerprint);
		CHECK(store.begin()->private_key().has_value());
	}

	SECTION("p12_extension")
	{
		const scoped_dir dir{"pal_cert_dir_p12"};
		dir.write_pkcs12("identity.p12");

		auto store = certificate_store::from_cert_dir(dir.path).value();
		CHECK_FALSE(store.empty());
		CHECK(store.begin()->fingerprint() == test_cert::server.fingerprint);
	}

	SECTION("case_insensitive_extension")
	{
		const scoped_dir dir{"pal_cert_dir_case"};
		dir.write_pkcs12("identity.PFX");

		auto store = certificate_store::from_cert_dir(dir.path).value();
		CHECK_FALSE(store.empty());
	}

	SECTION("non_pkcs12_files_ignored")
	{
		const scoped_dir dir{"pal_cert_dir_mixed"};
		dir.write_pkcs12("identity.pfx");
		dir.write_file("notes.txt", std::as_bytes(std::span{std::string_view{"hello"}}));
		dir.write_file("readme", std::as_bytes(std::span{std::string_view{"hello"}}));

		auto store = certificate_store::from_cert_dir(dir.path).value();
		CHECK(std::ranges::distance(store.begin(), store.end()) == 4);
	}

	SECTION("bad_pkcs12_skipped")
	{
		const scoped_dir dir{"pal_cert_dir_bad"};
		dir.write_pkcs12("good.pfx");
		dir.write_file("bad.pfx", std::as_bytes(std::span{std::string_view{"garbage"}}));

		auto store = certificate_store::from_cert_dir(dir.path).value();
		CHECK_FALSE(store.empty());
		CHECK(store.begin()->fingerprint() == test_cert::server.fingerprint);
	}

	SECTION("only_bad_files")
	{
		const scoped_dir dir{"pal_cert_dir_only_bad"};
		dir.write_file("bad.pfx", std::as_bytes(std::span{std::string_view{"garbage"}}));

		auto store = certificate_store::from_cert_dir(dir.path).value();
		CHECK(store.empty());
		CHECK_FALSE(store.is_null());
	}

	SECTION("sort_order")
	{
		const scoped_dir dir{"pal_cert_dir_sort"};

		// Write distinguishable payloads:
		// - a.pfx = RSA chain (4 certs, leaf = server),
		// - b.pfx = EC single cert (leaf = empty_dn).
		//
		// If sort works, the EC leaf must appear AFTER the RSA chain.
		dir.write_file("b.pfx", std::as_bytes(std::span{test_cert::pkcs12_ec_data}));
		dir.write_pkcs12("a.pfx");

		auto store = certificate_store::from_cert_dir(dir.path).value();
		std::vector<std::string_view> fps;
		std::ranges::transform(store, std::back_inserter(fps), &certificate::fingerprint);
		REQUIRE(fps.size() == 5);
		CHECK(fps.front() == test_cert::server.fingerprint);
		CHECK(fps.back() == test_cert::empty_dn.fingerprint);
	}
}

TEST_CASE("crypto/certificate_store/from_user_identities")
{
#if __pal_crypto_openssl

	// OpenSSL backend: from_user_identities() == from_cert_dir($SSL_CERT_DIR),
	// falling back to current working directory when SSL_CERT_DIR is unset.

	SECTION("ssl_cert_dir_set")
	{
		const scoped_dir dir{"pal_cert_dir_system"};
		dir.write_pkcs12("identity.pfx");

		set_env("SSL_CERT_DIR", dir.path.string().c_str());

		auto store = certificate_store::from_user_identities().value();
		CHECK_FALSE(store.empty());
		CHECK(store.begin()->fingerprint() == test_cert::server.fingerprint);

		unset_env("SSL_CERT_DIR");
	}

	SECTION("ssl_cert_dir_unset_falls_back_to_cwd")
	{
		unset_env("SSL_CERT_DIR");
		// Must succeed regardless of CWD contents.
		auto store = certificate_store::from_user_identities();
		REQUIRE(store);
	}

#elif __pal_crypto_windows

	// Windows backend: reads CERT_SYSTEM_STORE_CURRENT_USER\MY. Cannot
	// assume any specific certificate is present in the test environment,
	// so this is a best-effort smoke test: the call must succeed, and if
	// the store is non-empty, iteration must complete without crashing.

	SECTION("smoke")
	{
		auto store = certificate_store::from_user_identities();
		REQUIRE(store);
		if (store->empty())
		{
			SUCCEED("MY store is empty on this host");
			return;
		}
		const auto count = std::ranges::distance(store->begin(), store->end());
		CHECK(count > 0);
	}

#endif
}

} // namespace
