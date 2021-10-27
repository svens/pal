#include <pal/crypto/key>
#include <pal/crypto/certificate>
#include <pal/crypto/error>
#include <pal/crypto/test>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>


namespace {


using namespace pal::crypto;
namespace test_cert = pal_test::cert;


std::pair<public_key, private_key> import_keys ()
{
	private_key priv;
	auto pkcs12 = pal_test::to_der(test_cert::pkcs12);
	auto chain = certificate::import_pkcs12(
		std::span{pkcs12},
		"TestPassword",
		&priv
	);
	REQUIRE(!chain.empty());
	REQUIRE(chain.front().public_key());
	REQUIRE(priv);
	return std::make_pair(chain.front().public_key(), std::move(priv));
}


auto &keys ()
{
	static auto keys_ = import_keys();
	return keys_;
}


constexpr const char *tags =
#if __pal_os_macos
	"[.][!nonportable]"
#else
	"[!nonportable]"
#endif
;


TEST_CASE("crypto/key", tags)
{
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

			public_key b = certificate::from_pem(test_cert::ca_pem)->public_key();
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
			auto key = certificate::from_pem(pem)->public_key();
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
		auto [_, key] = import_keys();
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

	SECTION("private_key.sign: invalid digest algorithm")
	{
		auto &[_, priv] = keys();
		char sig_buf[1024];
		auto sig = priv.sign<md5>(pal_test::case_name(), sig_buf);
		REQUIRE(!sig.has_value());
		CHECK(sig.error() == pal::crypto::errc::invalid_digest_algorithm);
	}
}


TEMPLATE_TEST_CASE("crypto/key", tags, sha1, sha256, sha384, sha512)
{
	auto &[pub, priv] = keys();
	REQUIRE(pub);
	REQUIRE(priv);
	const auto message = pal_test::case_name();
	char sig_buf[1024];

	SECTION("sign / verify_signature")
	{
		auto sig = priv.sign<TestType>(message, sig_buf).value();
		CHECK(sig.data() != nullptr);
		CHECK(sig.size() == 256);
		CHECK(pub.verify_signature<TestType>(message, sig).value());
	}

	SECTION("sign: no key")
	{
		private_key key;
		auto sig = key.sign<TestType>(message, sig_buf);
		REQUIRE(!sig);
		CHECK(sig.error() == pal::crypto::errc::no_key);
	}

	SECTION("verify_signature: no key")
	{
		auto sig = priv.sign<TestType>(message, sig_buf).value();
		public_key key;
		auto result = key.verify_signature<TestType>(message, sig);
		REQUIRE(!result);
		CHECK(result.error() == pal::crypto::errc::no_key);
	}

	SECTION("sign: too small signature buffer")
	{
		char buf[1];
		auto sig = priv.sign<TestType>(message, buf);
		REQUIRE(!sig);
		CHECK(sig.error() == std::errc::result_out_of_range);
	}

	SECTION("verify_signature: invalid digest algorithm")
	{
		auto sig = priv.sign<TestType>(message, sig_buf).value();
		auto result = pub.verify_signature<md5>(message, sig);
		REQUIRE(!result);
		CHECK(result.error() == pal::crypto::errc::invalid_digest_algorithm);
	}

	SECTION("verify_signature: invalid size signature")
	{
		auto sig = priv.sign<TestType>(message, sig_buf).value();
		sig = sig.first(sig.size() / 2);
		auto result = pub.verify_signature<TestType>(message, sig);
		CHECK(!result.value());
	}

	SECTION("verify_signature: invalid signature")
	{
		auto sig = priv.sign<TestType>(message, sig_buf).value();
		sig_buf[sig.size() / 2] ^= 1;
		auto result = pub.verify_signature<TestType>(message, sig);
		CHECK(!result.value());
	}

	SECTION("verify_signature: different message")
	{
		auto sig = priv.sign<TestType>("hello", sig_buf).value();
		auto result = pub.verify_signature<TestType>("goodbye", sig);
		CHECK(!result.value());
	}

	if constexpr (pal::is_linux_build)
	{
		SECTION("sign: alloc error")
		{
			pal_test::bad_alloc_once x;
			auto sig = priv.sign<TestType>(message, sig_buf);
			REQUIRE(!sig);
			CHECK(sig.error() == std::errc::not_enough_memory);
		}

		SECTION("verify_signature: alloc error")
		{
			auto sig = priv.sign<TestType>(message, sig_buf).value();
			pal_test::bad_alloc_once x;
			auto result = pub.verify_signature<TestType>(message, sig);
			REQUIRE(!result);
			CHECK(result.error() == std::errc::not_enough_memory);
		}
	}
}


} // namespace
