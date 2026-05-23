#include <pal/crypto/digest_algorithm.hpp>
#include <pal/crypto/hmac.hpp>
#include <pal/test.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include <string_view>
#include <unordered_map>

namespace
{

using namespace pal_test;
using namespace pal::crypto::algorithm;

// clang-format off
// NOLINTBEGIN(bugprone-throwing-static-initialization)

constexpr std::string_view hmac_key = "key";
constexpr std::string_view empty{};
constexpr std::string_view
	lazy_dog = "The quick brown fox jumps over the lazy dog",
	lazy_cog = "The quick brown fox jumps over the lazy cog",
	lazy_dog_cog = "The quick brown fox jumps over the lazy dogThe quick brown fox jumps over the lazy cog";

template <typename Algorithm>
struct test_data;

template <>
struct test_data<md5>
{
	static constexpr size_t digest_size = 16;
	static constexpr std::string_view empty_key_digest = "74e6f7298a9c2d168935f58c001bad88";

	static inline const std::unordered_map<std::string_view, std::string_view> expected =
	{
		{ empty, "63530468a04e386459855da0063b6596" },
		{ lazy_dog, "80070713463e7749b90c2dc24911e275" },
		{ lazy_cog, "f734cebb1ebaf1480795349e4a515799" },
		{ lazy_dog_cog, "d1edfe826f38af7c04ac1455611609ec" },
	};
};

template <>
struct test_data<sha1>
{
	static constexpr size_t digest_size = 20;
	static constexpr std::string_view empty_key_digest = "fbdb1d1b18aa6c08324b7d64b71fb76370690e1d";

	static inline const std::unordered_map<std::string_view, std::string_view> expected =
	{
		{ empty, "f42bb0eeb018ebbd4597ae7213711ec60760843f" },
		{ lazy_dog, "de7c9b85b8b78aa6bc8a7a36f70a90701c9db4d9" },
		{ lazy_cog, "ad8d3f85da865d37e37ae5d7ab8ee32c5681ebc1" },
		{ lazy_dog_cog, "3cef80fd41cf8c39116690cc24a14e8cfe286547" },
	};
};

template <>
struct test_data<sha256>
{
	static constexpr size_t digest_size = 32;
	static constexpr std::string_view empty_key_digest = "b613679a0814d9ec772f95d778c35fc5ff1697c493715653c6c712144292c5ad";

	static inline const std::unordered_map<std::string_view, std::string_view> expected =
	{
		{ empty, "5d5d139563c95b5967b9bd9a8c9b233a9dedb45072794cd232dc1b74832607d0" },
		{ lazy_dog, "f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8" },
		{ lazy_cog, "3f7d9044432ff5c2a390eea7dbb3fcbdbb7b51bb0089fa7354d135500e0bca36" },
		{ lazy_dog_cog, "da9a338b329a975ba651ecb3286de8dd96c616d6df8b477738e822e3bc889915" },
	};
};

template <>
struct test_data<sha384>
{
	static constexpr size_t digest_size = 48;
	static constexpr std::string_view empty_key_digest = "6c1f2ee938fad2e24bd91298474382ca218c75db3d83e114b3d4367776d14d3551289e75e8209cd4b792302840234adc";

	static inline const std::unordered_map<std::string_view, std::string_view> expected =
	{
		{ empty, "99f44bb4e73c9d0ef26533596c8d8a32a5f8c10a9b997d30d89a7e35ba1ccf200b985f72431202b891fe350da410e43f" },
		{ lazy_dog, "d7f4727e2c0b39ae0f1e40cc96f60242d5b7801841cea6fc592c5d3e1ae50700582a96cf35e1e554995fe4e03381c237" },
		{ lazy_cog, "c550bf5a491af63f266daa271c2a449323d5adbc405080cbe437190ab60b49b63bd436c159259484331a40540bb0787b" },
		{ lazy_dog_cog, "47b406402e9b10b32a4d87809bc19c5d381c8dc67514d44e688557bb09cc6c6efcf0e8e4f27eea2403754015f81af0b9" },
	};
};

template <>
struct test_data<sha512>
{
	static constexpr size_t digest_size = 64;
	static constexpr std::string_view empty_key_digest = "b936cee86c9f87aa5d3c6f2e84cb5a4239a5fe50480a6ec66b70ab5b1f4ac6730c6c515421b327ec1d69402e53dfb49ad7381eb067b338fd7b0cb22247225d47";

	static inline const std::unordered_map<std::string_view, std::string_view> expected =
	{
		{ empty, "84fa5aa0279bbc473267d05a53ea03310a987cecc4c1535ff29b6d76b8f1444a728df3aadb89d4a9a6709e1998f373566e8f824a8ca93b1821f0b69bc2a2f65e" },
		{ lazy_dog, "b42af09057bac1e2d41708e48a902e09b5ff7f12ab428a4fe86653c73dd248fb82f948a549f7b791a5b41915ee4d1ec3935357e4e2317250d0372afa2ebeeb3a" },
		{ lazy_cog, "f3e0fd665455729c1f1da82f7f72eb41d3a6b886f523a57f4c2e2bb1f081cc394c824de9371a1751d52ac496128efca5e6ac61a8536091eeb093c4f89ad9c5d6" },
		{ lazy_dog_cog, "ae06454efb6e7ae7dd3559c1e4f86d7e054717adec2ae1ae60d31927a6ee95d024f51da2999e8ec2277a2447a1f1c404a73025f3c0fd60d3058f00164f1314d7" },
	};
};

// NOLINTEND(bugprone-throwing-static-initialization)

TEMPLATE_TEST_CASE("crypto/hmac", "", md5, sha1, sha256, sha384, sha512)
{
	using HMAC = pal::crypto::basic_hmac<TestType>;
	CHECK(test_data<TestType>::digest_size == TestType::digest_size);
	CHECK(test_data<TestType>::digest_size == HMAC::digest_size);

	const auto &expected = test_data<TestType>::expected;

	SECTION("update")
	{
		auto hmac = HMAC::make(hmac_key).value();
		CHECK(to_hex(hmac.update(std::span{lazy_dog}).finish()) == expected.at(lazy_dog));
	}

	SECTION("no update")
	{
		auto hmac = HMAC::make(hmac_key).value();
		CHECK(to_hex(hmac.finish()) == expected.at(empty));
	}

	SECTION("reuse")
	{
		auto hmac = HMAC::make(hmac_key).value();
		CHECK(to_hex(hmac.update(std::span{lazy_dog}).finish()) == expected.at(lazy_dog));
		CHECK(to_hex(hmac.update(std::span{lazy_cog}).finish()) == expected.at(lazy_cog));
	}

	SECTION("multiple updates")
	{
		auto hmac = HMAC::make(hmac_key).value();
		hmac.update(std::span{lazy_dog}).update(std::span{lazy_cog});
		CHECK(to_hex(hmac.finish()) == expected.at(lazy_dog_cog));
	}

	SECTION("move assign")
	{
		auto h1 = HMAC::make(hmac_key).value();
		h1.update(std::span{lazy_dog});
		auto h2 = HMAC::make(hmac_key).value();
		h2 = std::move(h1);
		CHECK(to_hex(h2.finish()) == expected.at(lazy_dog));
	}

	SECTION("one_shot")
	{
		CHECK(to_hex(HMAC::one_shot(hmac_key, std::span{lazy_dog}).value()) == expected.at(lazy_dog));
	}

	SECTION("one_shot: multiple inputs")
	{
		CHECK(to_hex(HMAC::one_shot(hmac_key, std::span{lazy_dog}, std::span{lazy_cog}).value()) == expected.at(lazy_dog_cog));
	}

	SECTION("finish: output buffer")
	{
		auto hmac = HMAC::make(hmac_key).value();
		std::array<std::byte, 64> buf{};
		const auto out = hmac.update(std::span{lazy_dog}).finish(buf);
		REQUIRE(out.has_value());
		CHECK(out->size() == HMAC::digest_size);
		CHECK(to_hex(*out) == expected.at(lazy_dog));
	}

	SECTION("finish: output buffer too small")
	{
		auto hmac = HMAC::make(hmac_key).value();
		std::vector<std::byte> buf(HMAC::digest_size - 1);
		const auto out = hmac.finish(buf);
		REQUIRE(!out.has_value());
		CHECK(out.error() == std::errc::no_buffer_space);
	}

	SECTION("empty key")
	{
		auto hmac = HMAC::make(std::string_view{}).value();
		CHECK(to_hex(hmac.finish()) == test_data<TestType>::empty_key_digest);
	}

	SECTION("make: not_enough_memory")
	{
		const pal_test::bad_alloc_once x;
		auto h = HMAC::make(hmac_key);
		REQUIRE(!h.has_value());
		CHECK(h.error() == std::errc::not_enough_memory);
	}
}

// clang-format on

} // namespace
