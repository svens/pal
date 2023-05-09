#include <pal/crypto/digest_algorithm>
#include <pal/crypto/hash>
#include <pal/crypto/test>
#include <catch2/catch_template_test_macros.hpp>
#include <string>
#include <unordered_map>

namespace {

using namespace pal_test;
using namespace pal::crypto::algorithm;

static const std::string
	empty = "",
	lazy_dog = "The quick brown fox jumps over the lazy dog",
	lazy_cog = "The quick brown fox jumps over the lazy cog";

template <typename Algorithm>
struct test_data;

template <>
struct test_data<md5>
{
	static constexpr size_t digest_size = 16u;

	static inline const std::unordered_map<std::string, std::string> expected =
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
	using Hash = pal::crypto::basic_hash<TestType>;
	auto h = Hash::make().value();

	CHECK(test_data<TestType>::digest_size == TestType::digest_size);
	CHECK(h.digest_size == TestType::digest_size);

	const auto &expected = test_data<TestType>::expected;

	const std::array spans =
	{
		std::span{lazy_dog},
		std::span{lazy_cog},
	};

	SECTION("update")
	{
		CHECK(to_hex(h.update(std::span{lazy_dog}).finish()) == expected.at(lazy_dog));
	}

	SECTION("finish")
	{
		h.update(std::span{lazy_dog});
		uint8_t data[Hash::digest_size];
		*reinterpret_cast<typename Hash::digest_type *>(data) = h.finish();
		CHECK(to_hex(data) == expected.at(lazy_dog));
	}

	SECTION("no update")
	{
		CHECK(to_hex(h.finish()) == expected.at(empty));
	}

	SECTION("reuse")
	{
		CHECK(to_hex(h.update(std::span{lazy_dog}).finish()) == expected.at(lazy_dog));
		CHECK(to_hex(h.update(std::span{lazy_cog}).finish()) == expected.at(lazy_cog));
	}

	SECTION("multiple updates")
	{
		h.update(std::span{lazy_dog}).update(std::span{lazy_cog});
		CHECK(to_hex(h.finish()) == expected.at(lazy_dog + lazy_cog));
	}

	SECTION("multiple spans")
	{
		CHECK(to_hex(h.update(spans).finish()) == expected.at(lazy_dog + lazy_cog));
	}

	SECTION("one_shot")
	{
		CHECK(to_hex(Hash::one_shot(std::span{lazy_dog}).value()) == expected.at(lazy_dog));
	}

	SECTION("one_shot: multiple spans")
	{
		CHECK(to_hex(Hash::one_shot(spans).value()) == expected.at(lazy_dog + lazy_cog));
	}

	SECTION("make: not_enough_memory")
	{
		pal_test::bad_alloc_once x;
		auto hasher = Hash::make();
		REQUIRE(!hasher.has_value());
		CHECK(hasher.error() == std::errc::not_enough_memory);
	}
}

} // namespace
