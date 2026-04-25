#include <pal/masked_formatter.hpp>
#include <catch2/catch_test_macros.hpp>

namespace
{

struct point
{
	int x, y;
};

} // namespace

template <>
struct pal::masked_formatter<point>
{
	template <typename FormatContext>
	static FormatContext::iterator format (const point &, FormatContext &ctx)
	{
		return std::format_to(ctx.out(), "(?,?)");
	}
};

namespace
{

TEST_CASE("masked_formatter")
{
	const point p{.x = 1, .y = 2};
	CHECK(std::format("{}", pal::masked{p}) == "(?,?)");
}

} // namespace
