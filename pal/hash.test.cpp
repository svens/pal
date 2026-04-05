#include <pal/hash.hpp>
#include <pal/test.hpp>
#include <catch2/catch_test_macros.hpp>
#include <string_view>

namespace
{

using namespace std::string_view_literals;

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
}

} // namespace
