#include <pal/crypto/random>
#include <pal/crypto/test>
#include <catch2/catch_test_macros.hpp>
#include <array>


namespace {


TEST_CASE("crypto/random")
{
	SECTION("nullptr")
	{
		std::span<int> span;
		pal::crypto::random(span);
		CHECK(span.size() == 0);
	}

	SECTION("empty")
	{
		int data[1];
		std::span span{data, data};
		pal::crypto::random(span);
		CHECK(span.size() == 0);
	}

	SECTION("single")
	{
		std::string initial = "test", data = initial;
		pal::crypto::random(std::span{data});
		CHECK(data != initial);
	}

	SECTION("multiple")
	{
		std::string one = "one", two = "two";
		std::array spans =
		{
			std::span{one},
			std::span{two},
		};
		pal::crypto::random(spans);
		CHECK(one != "one");
		CHECK(two != "two");
	}
}


} // namespace
