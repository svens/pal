#include <pal/crypto/signature.hpp>
#include <pal/crypto/hash.hpp>
#include <pal/crypto/certificate_store.hpp>
#include <pal/crypto/test.hpp>
#include <catch2/catch_template_test_macros.hpp>

namespace
{

using namespace pal::crypto::algorithm;
using namespace pal::crypto::scheme;
namespace test_cert = pal_test::cert;

struct bad_scheme
{
	static constexpr std::string_view id = "bad";
	static constexpr bool is_deterministic = false;
};
static_assert(pal::crypto::signature_scheme<bad_scheme>);

struct bad_digest
{
	static constexpr std::string_view id = "bad";
	static constexpr size_t digest_size = 32;
	using hash = decltype(std::ignore);
	using hmac = decltype(std::ignore);
};
static_assert(pal::crypto::digest_algorithm<bad_digest>);

template <pal::crypto::signature_scheme Scheme, pal::crypto::digest_algorithm Digest>
struct with
{
	using signature = pal::crypto::signature<Scheme, Digest>;
	using provider_type = signature::provider;
	using verifier_type = signature::verifier;
	using signature_buffer = std::array<std::byte, test_cert::server.max_block_size>;
	using hash_type = pal::crypto::basic_hash<Digest>;

	static auto &store ()
	{
		static auto store_ = pal::crypto::certificate_store::from_pkcs12(test_cert::pkcs12_data).value();
		return store_;
	}

	static auto &public_key ()
	{
		static auto key_ = store().begin()->public_key().value();
		return key_;
	}

	static auto &private_key ()
	{
		static auto key_ = store().begin()->private_key().value();
		return key_;
	}

	static auto &provider ()
	{
		static auto provider_ = provider_type::make(private_key()).value();
		return provider_;
	}

	static auto &verifier ()
	{
		static auto verifier_ = verifier_type::make(public_key()).value();
		return verifier_;
	}
};

template <pal::crypto::digest_algorithm Digest>
struct with<ecdsa, Digest>
{
	using signature = pal::crypto::signature<ecdsa, Digest>;
	using provider_type = signature::provider;
	using verifier_type = signature::verifier;
	using signature_buffer = std::array<std::byte, test_cert::empty_dn.max_block_size>;
	using hash_type = pal::crypto::basic_hash<Digest>;

	static auto &store ()
	{
		static auto store_ = pal::crypto::certificate_store::from_pkcs12(test_cert::pkcs12_ec_data).value();
		return store_;
	}

	static auto &public_key ()
	{
		static auto key_ = store().begin()->public_key().value();
		return key_;
	}

	static auto &private_key ()
	{
		static auto key_ = store().begin()->private_key().value();
		return key_;
	}

	static auto &provider ()
	{
		static auto provider_ = provider_type::make(private_key()).value();
		return provider_;
	}

	static auto &verifier ()
	{
		static auto verifier_ = verifier_type::make(public_key()).value();
		return verifier_;
	}
};

TEMPLATE_TEST_CASE(
	"crypto/signature",
	"",
	(with<pkcs1, sha1>),
	(with<pkcs1, sha256>),
	(with<pkcs1, sha384>),
	(with<pkcs1, sha512>),
	(with<pss, sha1>),
	(with<pss, sha256>),
	(with<pss, sha384>),
	(with<pss, sha512>),
	(with<ecdsa, sha1>),
	(with<ecdsa, sha256>),
	(with<ecdsa, sha384>),
	(with<ecdsa, sha512>)
)
{
	auto &p = TestType::provider();
	auto &v = TestType::verifier();
	auto digest = TestType::hash_type::one_shot(pal_test::case_name()).value();

	typename TestType::signature_buffer signature{};

	SECTION("sign and verify")
	{
		auto span = p.sign(digest, signature).value();
		REQUIRE(span.data() == reinterpret_cast<const std::byte *>(signature.data()));
		REQUIRE(span.size() <= p.signature_size());
		CHECK(v.is_valid(digest, span));
	}

	SECTION("signature_size")
	{
		CHECK(p.signature_size() == TestType::private_key().max_block_size());
		CHECK(v.signature_size() == TestType::public_key().max_block_size());
	}

	SECTION("is_valid: wrong digest")
	{
		auto span = p.sign(digest, signature).value();
		digest[0] ^= std::byte{1};
		CHECK_FALSE(v.is_valid(digest, span));
	}

	SECTION("is_valid: wrong signature")
	{
		auto span = p.sign(digest, signature).value();
		signature[0] ^= std::byte{1};
		CHECK_FALSE(v.is_valid(digest, span));
	}

	SECTION("sign: no buffer space")
	{
		std::array<std::byte, 1> tiny{};
		auto result = p.sign(digest, tiny);
		REQUIRE_FALSE(result);
		CHECK(result.error() == std::errc::no_buffer_space);
	}

	SECTION("provider: public key not supported")
	{
		auto tmp = TestType::provider_type::make(TestType::public_key());
		REQUIRE_FALSE(tmp);
		CHECK(tmp.error() == std::errc::not_supported);
	}

	SECTION("verifier: private key not supported")
	{
		auto tmp = TestType::verifier_type::make(TestType::private_key());
		REQUIRE_FALSE(tmp);
		CHECK(tmp.error() == std::errc::not_supported);
	}

	SECTION("provider: not enough memory")
	{
		const pal_test::bad_alloc_once x{};
		auto tmp = TestType::provider_type::make(TestType::private_key());
		REQUIRE_FALSE(tmp);
		CHECK(tmp.error() == std::errc::not_enough_memory);
	}

	SECTION("verifier: not enough memory")
	{
		const pal_test::bad_alloc_once x{};
		auto tmp = TestType::verifier_type::make(TestType::public_key());
		REQUIRE_FALSE(tmp);
		CHECK(tmp.error() == std::errc::not_enough_memory);
	}
}

TEST_CASE("crypto/signature")
{
	using rsa_pkcs1 = with<pkcs1, sha256>;
	using rsa_pss = with<pss, sha256>;
	using ec_ecdsa = with<ecdsa, sha256>;

	SECTION("provider: ec key with pkcs1")
	{
		auto tmp = rsa_pkcs1::provider_type::make(ec_ecdsa::public_key());
		REQUIRE_FALSE(tmp);
		CHECK(tmp.error() == std::errc::not_supported);
	}

	SECTION("verifier: ec key with pkcs1")
	{
		auto tmp = rsa_pkcs1::verifier_type::make(ec_ecdsa::public_key());
		REQUIRE_FALSE(tmp);
		CHECK(tmp.error() == std::errc::not_supported);
	}

	SECTION("provider: ec key with pss")
	{
		auto tmp = rsa_pss::provider_type::make(ec_ecdsa::public_key());
		REQUIRE_FALSE(tmp);
		CHECK(tmp.error() == std::errc::not_supported);
	}

	SECTION("verifier: ec key with pss")
	{
		auto tmp = rsa_pss::verifier_type::make(ec_ecdsa::public_key());
		REQUIRE_FALSE(tmp);
		CHECK(tmp.error() == std::errc::not_supported);
	}

	SECTION("provider: rsa key with ecdsa")
	{
		auto tmp = ec_ecdsa::provider_type::make(rsa_pkcs1::public_key());
		REQUIRE_FALSE(tmp);
		CHECK(tmp.error() == std::errc::not_supported);
	}

	SECTION("verifier: rsa key with ecdsa")
	{
		auto tmp = ec_ecdsa::verifier_type::make(rsa_pkcs1::public_key());
		REQUIRE_FALSE(tmp);
		CHECK(tmp.error() == std::errc::not_supported);
	}

	SECTION("provider: unknown scheme")
	{
		auto tmp = pal::crypto::signature<bad_scheme, sha256>::provider::make(rsa_pkcs1::public_key());
		REQUIRE_FALSE(tmp);
		CHECK(tmp.error() == std::errc::not_supported);
	}

	SECTION("provider: unknown digest")
	{
		auto tmp = pal::crypto::signature<pkcs1, bad_digest>::provider::make(rsa_pkcs1::private_key());
		REQUIRE_FALSE(tmp);
		CHECK(tmp.error() == std::errc::not_supported);
	}
}

} // namespace
