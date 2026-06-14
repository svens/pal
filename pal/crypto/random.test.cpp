#include <pal/crypto/random.hpp>
#include <pal/crypto/test.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <string>

namespace
{

struct uuid_t
{
	uint8_t data[16];
	bool operator== (const uuid_t &) const = default;
};

TEST_CASE("crypto/random")
{
	SECTION("buffer")
	{
		const std::string zero(16, '\0');
		auto buf = zero;
		REQUIRE(pal::crypto::random(buf));
		CHECK(buf != zero);
	}

	SECTION("buffer/empty")
	{
		std::string buf;
		REQUIRE(pal::crypto::random(buf));
	}

	SECTION("trivially_copyable/uint32_t")
	{
		auto r = pal::crypto::random<uint32_t>();
		REQUIRE(r);
		CHECK(*r != 0);
	}

	SECTION("trivially_copyable/uint64_t")
	{
		auto r = pal::crypto::random<uint64_t>();
		REQUIRE(r);
		CHECK(*r != 0);
	}

	SECTION("trivially_copyable/struct")
	{
		auto r = pal::crypto::random<uuid_t>();
		REQUIRE(r);
		CHECK(*r != uuid_t{});
	}
}

} // namespace
