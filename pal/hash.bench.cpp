#include <pal/hash>
#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators_range.hpp>


namespace {


TEST_CASE("hash", "[!benchmark]")
{
	std::string blob(1ull << GENERATE(range(2, 10)), 'x');
	BENCHMARK("fnv_1a_64/" + std::to_string(blob.size()))
	{
		return pal::fnv_1a_64(blob);
	};
}


} // namespace
