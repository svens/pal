#include <pal/result.hpp>
#include <pal/test.hpp>
#include <catch2/catch_test_macros.hpp>

namespace
{

TEST_CASE("result")
{
	SECTION("make_unexpected")
	{
		auto u = pal::make_unexpected(std::errc::not_enough_memory);
		CHECK(u.error() == std::errc::not_enough_memory);
	}

	SECTION("make_unexpected into result")
	{
		pal::result<int> r = pal::make_unexpected(std::errc::not_enough_memory);
		REQUIRE_FALSE(r.has_value());
		CHECK(r.error() == std::errc::not_enough_memory);
	}

	SECTION("result is expected")
	{
		static_assert(std::is_same_v<pal::result<int>, std::expected<int, std::error_code>>);

		static_assert(std::is_same_v<pal::result<void>, std::expected<void, std::error_code>>);
	}
}

} // namespace
