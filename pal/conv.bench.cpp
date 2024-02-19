#include <pal/conv>
#include <pal/test>
#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <random>
#include <span>

namespace {

using pal::base64;
using pal::hex;

void random (std::span<char> span) noexcept
{
	std::mt19937 rng;
	rng.seed(std::random_device()());
	std::uniform_int_distribution<std::mt19937::result_type> gen(0, 255);
	for (auto &v: span)
	{
		v = static_cast<char>(gen(rng));
	}
}

void random (std::span<char> span, std::string_view alphabet) noexcept
{
	std::mt19937 rng;
	rng.seed(std::random_device()());
	std::uniform_int_distribution<std::mt19937::result_type> gen(0, (unsigned)alphabet.size());
	for (auto &v: span)
	{
		v = alphabet[gen(rng)];
	}
}

template <typename Algorithm>
constexpr std::string_view alphabet{};

template <>
constexpr std::string_view alphabet<base64> =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
	"abcdefghijklmnopqrstuvwxyz"
	"0123456789"
	"+/";

template <>
constexpr std::string_view alphabet<hex> =
	"0123456789abcdefABCDEF";

TEMPLATE_TEST_CASE("conv", "[!benchmark]",
	base64,
	hex)
{
	auto size = 1ull << GENERATE(range(2, 10));
	char buffer[512];
	std::span in{buffer, size};
	char out[(std::max)({pal::encode_size<TestType>(buffer), pal::decode_size<TestType>(buffer)})];

	BENCHMARK_ADVANCED("encode/" + std::to_string(size))(auto meter)
	{
		random(in);
		meter.measure([&]{ return pal::encode<TestType>(in, out); });
	};

	BENCHMARK_ADVANCED("decode/" + std::to_string(size))(auto meter)
	{
		random(in, alphabet<TestType>);
		meter.measure([&]{ return pal::decode<TestType>(in, out); });
	};
}

} // namespace
