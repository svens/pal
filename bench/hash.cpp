#include <benchmark/benchmark.h>
#include <pal/crypto/random>
#include <pal/hash>


namespace {


void fnv_1a_64 (benchmark::State &state)
{
	const auto size = state.range(0);

	uint8_t input[512];
	std::span span(input, size);
	pal::crypto::random(span);

	for (auto _: state)
	{
		input[0] = static_cast<uint8_t>(state.iterations());
		auto result = pal::fnv_1a_64(input, input + size);
		benchmark::DoNotOptimize(result);
	}

	state.SetBytesProcessed(state.iterations() * size);
}
BENCHMARK(fnv_1a_64)
  ->RangeMultiplier(2)->Range(4, 2<<8)
  ->Complexity()
;


} // namespace
