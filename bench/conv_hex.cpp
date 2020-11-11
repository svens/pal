#include <benchmark/benchmark.h>
#include <pal/conv>
#include <random>


namespace {


void gen_to_hex_data (char *first, char *last) noexcept
{
	std::mt19937 rng;
	rng.seed(std::random_device()());
	std::uniform_int_distribution<std::mt19937::result_type> gen(0, 255);
	while (first != last)
	{
		*first++ = gen(rng);
	}
}


void conv_to_hex (benchmark::State &state)
{
	char in[512];
	char out[pal::to_hex_size(in, in + sizeof(in)).max_size];

	const auto size = state.range(0);
	if (size > sizeof(in))
	{
		state.SkipWithError("invalid size");
	}

	auto first = in, last = in + size;
	gen_to_hex_data(first, last);

	for (auto _: state)
	{
		auto result = pal::to_hex(first, last, out);
		benchmark::DoNotOptimize(result);
	}

	state.SetBytesProcessed(state.iterations() * size);
}
BENCHMARK(conv_to_hex)
  ->RangeMultiplier(2)->Range(8, 2<<8)
  ->Complexity()
;


void gen_from_hex_data (char *first, char *last) noexcept
{
	static constexpr char alphabet[] = "0123456789abcdefABCDEF";

	std::mt19937 rng;
	rng.seed(std::random_device()());
	std::uniform_int_distribution<std::mt19937::result_type> gen(
		0, sizeof(alphabet) - 2
	);
	while (first != last)
	{
		*first++ = alphabet[gen(rng)];
	}
}


void conv_from_hex (benchmark::State &state)
{
	char in[512];
	char out[pal::from_hex_size(in, in + sizeof(in)).max_size];

	const auto size = state.range(0);
	if (size > sizeof(in))
	{
		state.SkipWithError("invalid size");
	}

	auto first = in, last = in + size;
	gen_from_hex_data(first, last);

	for (auto _: state)
	{
		auto result = pal::from_hex(first, last, out);
		benchmark::DoNotOptimize(result);
	}

	state.SetBytesProcessed(state.iterations() * size);
}
BENCHMARK(conv_from_hex)
  ->RangeMultiplier(2)->Range(8, 2<<8)
  ->Complexity()
;


} // namespace
