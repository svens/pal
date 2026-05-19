#include <pal/crypto/random.hpp>
#include <pal/test.hpp>
#include <catch2/catch_test_macros.hpp>
#include <array>
#include <string>

namespace
{

TEST_CASE("crypto/random")
{
	const std::string zero(16, '\0');

	SECTION("empty")
	{
		std::string buf;
		REQUIRE(pal::crypto::random(buf));
	}

	SECTION("single")
	{
		auto buf = zero;
		REQUIRE(pal::crypto::random(buf));
		CHECK(buf != zero);
	}

	SECTION("multiple")
	{
		std::string one = zero, two = zero;
		std::array bufs = {std::span{one}, std::span{two}};
		REQUIRE(pal::crypto::random(bufs));
		CHECK(one != zero);
		CHECK(two != zero);
	}
}

} // namespace
