#include <pal/crypto/hash>
#include <pal/crypto/test>
#include <catch2/catch_template_test_macros.hpp>
#include <string>
#include <unordered_map>

namespace {

using namespace pal_test;

static const std::string
	empty = "",
	lazy_dog = "The quick brown fox jumps over the lazy dog",
	lazy_cog = "The quick brown fox jumps over the lazy cog";

struct md5
{
	using hash_type = pal::crypto::md5_hash;

	static inline std::unordered_map<std::string, std::string> expected =
	{
		{ empty, "d41d8cd98f00b204e9800998ecf8427e" },
		{ lazy_dog, "9e107d9d372bb6826bd81d3542a419d6" },
		{ lazy_cog, "1055d3e698d289f2af8663725127bd4b" },
		{ lazy_dog + lazy_cog, "29b4e7d924350ff800471c80c9ca2a3f" },
	};
};

TEMPLATE_TEST_CASE("crypto/hash", "",
	md5)
{
	using Hash = typename TestType::hash_type;

	SECTION("update")
	{
		Hash h;
		h.update(std::span{lazy_dog});
		CHECK(to_hex(h.finish()) == TestType::expected[lazy_dog]);
	}

	SECTION("finish")
	{
		Hash h;
		h.update(std::span{lazy_dog});
		uint8_t data[Hash::digest_size];
		*reinterpret_cast<typename Hash::digest_type *>(data) = h.finish();
		CHECK(to_hex(data) == TestType::expected[lazy_dog]);
	}

	SECTION("no update")
	{
		Hash h;
		CHECK(to_hex(h.finish()) == TestType::expected[empty]);
	}

	SECTION("reuse")
	{
		Hash h;

		h.update(std::span{lazy_dog});
		CHECK(to_hex(h.finish()) == TestType::expected[lazy_dog]);

		h.update(std::span{lazy_cog});
		CHECK(to_hex(h.finish()) == TestType::expected[lazy_cog]);
	}

	SECTION("multiple updates")
	{
		Hash h;
		h.update(std::span{lazy_dog}).update(std::span{lazy_cog});
		CHECK(to_hex(h.finish()) == TestType::expected[lazy_dog + lazy_cog]);
	}

	SECTION("multiple spans")
	{
		Hash h;
		std::array spans =
		{
			std::span{lazy_dog},
			std::span{lazy_cog},
		};
		CHECK(to_hex(h.update(spans).finish()) == TestType::expected[lazy_dog + lazy_cog]);
	}

	SECTION("one_shot")
	{
		auto r = Hash::one_shot(std::span{lazy_dog});
		CHECK(to_hex(r) == TestType::expected[lazy_dog]);
	}

	SECTION("one_shot: multiple spans")
	{
		std::array spans =
		{
			std::span{lazy_dog},
			std::span{lazy_cog},
		};
		auto r = Hash::one_shot(spans);
		CHECK(to_hex(r) == TestType::expected[lazy_dog + lazy_cog]);
	}

	auto h = Hash::make().value_or(nullptr);
	REQUIRE(h);
	CHECK(h->digest_size() == Hash::digest_size);

	SECTION("any_hash")
	{
		h->update(std::span{lazy_dog});
		uint8_t digest[Hash::digest_size];
		CHECK(h->finish(std::span{digest}).value() == sizeof(digest));
		CHECK(to_hex(digest) == TestType::expected[lazy_dog]);
	}

	SECTION("any_hash: multiple spans")
	{
		std::array spans =
		{
			std::span{lazy_dog},
			std::span{lazy_cog},
		};
		h->update(spans);
		uint8_t digest[Hash::digest_size];
		CHECK(h->finish(std::span{digest}).value() == sizeof(digest));
		CHECK(to_hex(digest) == TestType::expected[lazy_dog + lazy_cog]);
	}

	SECTION("any_hash: finish overflow")
	{
		h->update(std::span{lazy_dog});
		uint8_t digest[Hash::digest_size / 2];
		CHECK(h->finish(std::span{digest}).error() == std::errc::invalid_argument);
	}
}

} // namespace
