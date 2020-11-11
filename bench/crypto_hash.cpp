#include <benchmark/benchmark.h>
#include <pal/crypto/hash>
#include <pal/crypto/random>


namespace {


using md5 = pal::crypto::md5_hash;
using sha1 = pal::crypto::sha1_hash;
using sha256 = pal::crypto::sha256_hash;
using sha384 = pal::crypto::sha384_hash;
using sha512 = pal::crypto::sha512_hash;


template <typename Hasher>
void crypto_hash (benchmark::State &state)
{
	const auto size = state.range(0);

	uint8_t input[512];
	std::span span(input, size);
	pal::crypto::random(span);

	Hasher hash;
	for (auto _: state)
	{
		input[0] = static_cast<uint8_t>(state.iterations());
		auto result = hash.update(span).finish();
		benchmark::DoNotOptimize(result);
	}

	state.SetBytesProcessed(state.iterations() * size);
}
BENCHMARK_TEMPLATE(crypto_hash, md5)
  ->RangeMultiplier(2)->Range(32, 2<<8)
  ->Complexity()
;
BENCHMARK_TEMPLATE(crypto_hash, sha1)
  ->RangeMultiplier(2)->Range(32, 2<<8)
  ->Complexity()
;
BENCHMARK_TEMPLATE(crypto_hash, sha256)
  ->RangeMultiplier(2)->Range(32, 2<<8)
  ->Complexity()
;
BENCHMARK_TEMPLATE(crypto_hash, sha384)
  ->RangeMultiplier(2)->Range(32, 2<<8)
  ->Complexity()
;
BENCHMARK_TEMPLATE(crypto_hash, sha512)
  ->RangeMultiplier(2)->Range(32, 2<<8)
  ->Complexity()
;


template <typename Hasher>
void crypto_hash_one_shot (benchmark::State &state)
{
	const auto size = state.range(0);

	uint8_t input[512];
	std::span span(input, size);
	pal::crypto::random(span);

	for (auto _: state)
	{
		input[0] = static_cast<uint8_t>(state.iterations());
		auto result = Hasher::one_shot(span);
		benchmark::DoNotOptimize(result);
	}

	state.SetBytesProcessed(state.iterations() * size);
}
BENCHMARK_TEMPLATE(crypto_hash_one_shot, md5)
  ->RangeMultiplier(2)->Range(32, 2<<8)
  ->Complexity()
;
BENCHMARK_TEMPLATE(crypto_hash_one_shot, sha1)
  ->RangeMultiplier(2)->Range(32, 2<<8)
  ->Complexity()
;
BENCHMARK_TEMPLATE(crypto_hash_one_shot, sha256)
  ->RangeMultiplier(2)->Range(32, 2<<8)
  ->Complexity()
;
BENCHMARK_TEMPLATE(crypto_hash_one_shot, sha384)
  ->RangeMultiplier(2)->Range(32, 2<<8)
  ->Complexity()
;
BENCHMARK_TEMPLATE(crypto_hash_one_shot, sha512)
  ->RangeMultiplier(2)->Range(32, 2<<8)
  ->Complexity()
;


} // namespace
