#include <benchmark/benchmark.h>
#include <pal/conv>
#include <random>


namespace {


void gen_to_base64_data (std::span<char> span) noexcept
{
	std::mt19937 rng;
	rng.seed(std::random_device()());
	std::uniform_int_distribution<std::mt19937::result_type> gen(0, 255);

	auto first = span.data(), last = first + span.size_bytes();
	while (first != last)
	{
		*first++ = gen(rng);
	}
}


void conv_to_base64 (benchmark::State &state)
{
	char in[512];
	char out[pal::to_base64_size(in)];

	const auto size = state.range(0);
	if (size > sizeof(in))
	{
		state.SkipWithError("invalid size");
	}

	std::span input(in, size);
	gen_to_base64_data(input);

	for (auto _: state)
	{
		auto result = pal::to_base64(input, out);
		benchmark::DoNotOptimize(result);
	}

	state.SetBytesProcessed(state.iterations() * size);
}
BENCHMARK(conv_to_base64)
  ->RangeMultiplier(2)->Range(8, 2<<8)
  ->Complexity()
;


void gen_from_base64_data (std::span<char> span) noexcept
{
	const char alphabet[] =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
		"abcdefghijklmnopqrstuvwxyz"
		"0123456789"
		"+/";

	std::mt19937 rng;
	rng.seed(std::random_device()());
	std::uniform_int_distribution<std::mt19937::result_type> gen(
		0, sizeof(alphabet) - 2
	);

	auto first = span.data(), last = first + span.size_bytes();
	while (first != last)
	{
		*first++ = alphabet[gen(rng)];
	}
}


void conv_from_base64 (benchmark::State &state)
{
	char in[512];
	char out[pal::from_base64_size(in)];

	const auto size = state.range(0);
	if (size > sizeof(in))
	{
		state.SkipWithError("invalid size");
	}

	std::span input(in, size);
	gen_from_base64_data(input);

	for (auto _: state)
	{
		auto result = pal::from_base64(input, out);
		benchmark::DoNotOptimize(result);
	}

	state.SetBytesProcessed(state.iterations() * size);
}
BENCHMARK(conv_from_base64)
  ->RangeMultiplier(2)->Range(8, 2<<8)
  ->Complexity()
;


} // namespace
