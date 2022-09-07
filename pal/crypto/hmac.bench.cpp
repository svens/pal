#include <pal/crypto/hmac>
#include <pal/crypto/random>
#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/generators/catch_generators_range.hpp>


namespace {


using md5 = pal::crypto::md5_hmac;
using sha1 = pal::crypto::sha1_hmac;
using sha256 = pal::crypto::sha256_hmac;
using sha384 = pal::crypto::sha384_hmac;
using sha512 = pal::crypto::sha512_hmac;


TEMPLATE_TEST_CASE("crypto/hmac", "[!benchmark]",
	md5,
	sha1,
	sha256,
	sha384,
	sha512)
{
	size_t size = 1 << GENERATE(range(5, 10));
	uint8_t buffer[512];
	std::span span{buffer, size};
	pal::crypto::random(span);

	BENCHMARK_ADVANCED("update+finish/" + std::to_string(size))(Catch::Benchmark::Chronometer meter)
	{
		TestType hmac;
		meter.measure([&](int i)
		{
			buffer[0] = static_cast<uint8_t>(i);
			return hmac.update(span).finish();
		});
	};

	BENCHMARK_ADVANCED("one_shot/" + std::to_string(size))(Catch::Benchmark::Chronometer meter)
	{
		meter.measure([&](int i)
		{
			buffer[0] = static_cast<uint8_t>(i);
			return TestType::one_shot(span);
		});
	};
}


} // namespace
