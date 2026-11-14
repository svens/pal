#include <pal/hash.hpp>
#include <pal/test.hpp>
#include <catch2/catch_test_macros.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <list>
#include <string_view>

namespace
{

using namespace std::string_view_literals;

uint32_t crc32_bitwise (const uint8_t *first, const uint8_t *last)
{
	uint32_t crc = 0xffffffffU;
	while (first != last)
	{
		crc ^= *first++;
		for (int bit = 0; bit < 8; ++bit)
		{
			crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
		}
	}
	return ~crc;
}

// Official FNV-1a 64-bit test vectors
// http://www.isthe.com/chongo/tech/comp/fnv/

TEST_CASE("hash")
{
	SECTION("fnv_1a_64: empty")
	{
		auto empty = ""sv;
		CHECK(pal::fnv_1a_64(empty) == 0xcbf29ce484222325ULL);
	}

	SECTION("fnv_1a_64: known vectors")
	{
		CHECK(pal::fnv_1a_64("a"sv) == 0xaf63dc4c8601ec8cULL);
		CHECK(pal::fnv_1a_64("b"sv) == 0xaf63df4c8601f1a5ULL);
		CHECK(pal::fnv_1a_64("c"sv) == 0xaf63de4c8601eff2ULL);
		CHECK(pal::fnv_1a_64("foobar"sv) == 0x85944171f73967e8ULL);
	}

	SECTION("fnv_1a_64: iterator range")
	{
		auto data = "foobar"sv;
		auto h = pal::fnv_1a_64(data.begin(), data.end());
		CHECK(h == pal::fnv_1a_64(data));
	}

	SECTION("fnv_1a_64: chained hashing")
	{
		auto full = "foobar"sv;
		auto h = pal::fnv_1a_64("foo"sv);
		h = pal::fnv_1a_64("bar"sv, h);
		CHECK(h == pal::fnv_1a_64(full));
	}

	SECTION("fnv_1a_64: constexpr")
	{
		constexpr auto h = pal::fnv_1a_64("test"sv);
		static_assert(h != pal::fnv_1a_64_init);
		CHECK(h != 0);
	}

	SECTION("hash_128_to_64")
	{
		auto a = pal::hash_128_to_64(1, 2);
		auto b = pal::hash_128_to_64(2, 1);
		CHECK(a != b);
	}

	SECTION("hash_128_to_64: same inputs")
	{
		auto a = pal::hash_128_to_64(0x1ULL, 0x2ULL);
		auto b = pal::hash_128_to_64(0x2ULL, 0x1ULL);
		CHECK(a != b);
	}

	SECTION("hash_128_to_64: constexpr")
	{
		constexpr auto h = pal::hash_128_to_64(1, 2);
		static_assert(h != 0);
		CHECK(h != 0);
	}

	SECTION("crc32: empty")
	{
		CHECK(pal::crc32(""sv) == 0);
	}

	SECTION("crc32: check value")
	{
		CHECK(pal::crc32("123456789"sv) == 0xcbf43926U);
	}

	SECTION("crc32: iterator range")
	{
		auto data = "123456789"sv;
		CHECK(pal::crc32(data.begin(), data.end()) == pal::crc32(data));
	}

	SECTION("crc32: non-random-access iterator")
	{
		auto data = "123456789"sv;
		std::list<char> list{data.begin(), data.end()};
		CHECK(pal::crc32(list) == pal::crc32(data));
	}

	SECTION("crc32: std::byte")
	{
		std::array<std::byte, 9> data{};
		for (size_t i = 0; i < data.size(); ++i)
		{
			data[i] = static_cast<std::byte>('1' + i);
		}
		CHECK(pal::crc32(data) == pal::crc32("123456789"sv));
	}

	SECTION("crc32: chained hashing")
	{
		auto full = "123456789abcdefghijklmnopqrstuvwxyz"sv;
		for (size_t split = 0; split <= full.size(); ++split)
		{
			auto crc = pal::crc32(full.substr(0, split));
			crc = pal::crc32(full.substr(split), crc);
			CHECK(crc == pal::crc32(full));
		}
	}

	SECTION("crc32: lengths match bitwise reference")
	{
		std::array<uint8_t, 64> data{};
		uint32_t seed = 0x12345678U;
		for (auto &b: data)
		{
			seed = seed * 1103515245U + 12345U;
			b = static_cast<uint8_t>(seed >> 16);
		}
		for (size_t size = 0; size <= data.size(); ++size)
		{
			CHECK(pal::crc32(data.data(), data.data() + size) == crc32_bitwise(data.data(), data.data() + size));
		}
	}

	SECTION("crc32: constexpr")
	{
		constexpr auto crc = pal::crc32("123456789"sv);
		static_assert(crc == 0xcbf43926U);
		CHECK(crc == 0xcbf43926U);
	}
}

} // namespace
