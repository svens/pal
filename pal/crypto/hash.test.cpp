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
	static constexpr size_t digest_size = 16;

	static inline const std::unordered_map<std::string, std::string> expected =
	{
		{ empty, "d41d8cd98f00b204e9800998ecf8427e" },
		{ lazy_dog, "9e107d9d372bb6826bd81d3542a419d6" },
		{ lazy_cog, "1055d3e698d289f2af8663725127bd4b" },
		{ lazy_dog + lazy_cog, "29b4e7d924350ff800471c80c9ca2a3f" },
	};
};

template <>
struct test_data<sha1>
{
	static constexpr size_t digest_size = 20;

	static inline const std::unordered_map<std::string, std::string> expected =
	{
		{ empty, "da39a3ee5e6b4b0d3255bfef95601890afd80709" },
		{ lazy_dog, "2fd4e1c67a2d28fced849ee1bb76e7391b93eb12" },
		{ lazy_cog, "de9f2c7fd25e1b3afad3e85a0bd17d9b100db4b3" },
		{ lazy_dog + lazy_cog, "38590c861cc71a4186b2909285a04609fb23bb42" },
	};
};

template <>
struct test_data<sha256>
{
	static constexpr size_t digest_size = 32;

	static inline const std::unordered_map<std::string, std::string> expected =
	{
		{ empty, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" },
		{ lazy_dog, "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592" },
		{ lazy_cog, "e4c4d8f3bf76b692de791a173e05321150f7a345b46484fe427f6acc7ecc81be" },
		{ lazy_dog + lazy_cog, "0a9a361e469fd8fb48e915a06431f3fabbfb0960226421a25ab939fde121b7c8" },
	};
};

template <>
struct test_data<sha384>
{
	static constexpr size_t digest_size = 48;

	static inline const std::unordered_map<std::string, std::string> expected =
	{
		{ empty, "38b060a751ac96384cd9327eb1b1e36a21fdb71114be07434c0cc7bf63f6e1da274edebfe76f65fbd51ad2f14898b95b" },
		{ lazy_dog, "ca737f1014a48f4c0b6dd43cb177b0afd9e5169367544c494011e3317dbf9a509cb1e5dc1e85a941bbee3d7f2afbc9b1" },
		{ lazy_cog, "098cea620b0978caa5f0befba6ddcf22764bea977e1c70b3483edfdf1de25f4b40d6cea3cadf00f809d422feb1f0161b" },
		{ lazy_dog + lazy_cog, "03b251e870443c1dc8052967970cc91bdd3bd5c3784ea0b2df52f0f4a6c56f947fcc1369b593730479dc07d73a043297" },
	};
};

template <>
struct test_data<sha512>
{
	static constexpr size_t digest_size = 64;

	static inline const std::unordered_map<std::string, std::string> expected =
	{
		{ empty, "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e" },
		{ lazy_dog, "07e547d9586f6a73f73fbac0435ed76951218fb7d0c8d788a309d785436bbb642e93a252a954f23912547d1e8a3b5ed6e1bfd7097821233fa0538f3db854fee6" },
		{ lazy_cog, "3eeee1d0e11733ef152a6c29503b3ae20c4f1f3cda4cb26f1bc1a41f91c7fe4ab3bd86494049e201c4bd5155f31ecb7a3c8606843c4cc8dfcab7da11c8ae5045" },
		{ lazy_dog + lazy_cog, "9a1eacc4b2de80d412e8e28aa918c22450246c9d249559e6cba45145feebd05298c8d91cde493acd7c2bf9ed5c86612a7f8c8323c10913d8b4703c8d6bcd99f8" },
	};
};

TEMPLATE_TEST_CASE("crypto/hash", "",
	md5,
	sha1,
	sha256,
	sha384,
	sha512)
{
	using Hash = pal::crypto::basic_hash<TestType>;
	CHECK(test_data<TestType>::digest_size == TestType::digest_size);
	CHECK(test_data<TestType>::digest_size == Hash::digest_size);

	auto hash = Hash::make().value();
	const auto &expected = test_data<TestType>::expected;

	const std::array spans =
	{
		std::span{lazy_dog},
		std::span{lazy_cog},
	};

	SECTION("update")
	{
		CHECK(to_hex(hash.update(std::span{lazy_dog}).finish()) == expected.at(lazy_dog));
	}

	SECTION("no update")
	{
		CHECK(to_hex(hash.finish()) == expected.at(empty));
	}

	SECTION("reuse")
	{
		CHECK(to_hex(hash.update(std::span{lazy_dog}).finish()) == expected.at(lazy_dog));
		CHECK(to_hex(hash.update(std::span{lazy_cog}).finish()) == expected.at(lazy_cog));
	}

	SECTION("multiple updates")
	{
		hash.update(std::span{lazy_dog}).update(std::span{lazy_cog});
		CHECK(to_hex(hash.finish()) == expected.at(lazy_dog + lazy_cog));
	}

	SECTION("multiple spans")
	{
		CHECK(to_hex(hash.update(spans).finish()) == expected.at(lazy_dog + lazy_cog));
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
		auto h = Hash::make();
		REQUIRE(!h.has_value());
		CHECK(h.error() == std::errc::not_enough_memory);
	}
}

} // namespace
