#include <pal/codec.hpp>
#include <pal/test.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <array>
#include <chrono>
#include <random>
#include <span>
#include <string_view>

namespace
{

// clang-format off

template <typename C>
constexpr std::string_view codec_alphabet{};

template <>
constexpr std::string_view codec_alphabet<pal::base64_decoder> =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
	"abcdefghijklmnopqrstuvwxyz"
	"0123456789+/"
;

template <>
constexpr std::string_view codec_alphabet<pal::hex_decoder> =
	"0123456789abcdefABCDEF"
;

// clang-format on

void fill_random (std::span<char> buf) noexcept
{
	std::mt19937 rng{static_cast<uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count())};
	std::uniform_int_distribution<unsigned> dist{0, 255};
	for (auto &c: buf)
	{
		c = static_cast<char>(dist(rng));
	}
}

void fill_random (std::span<char> buf, std::string_view alpha) noexcept
{
	std::mt19937 rng{static_cast<uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count())};
	std::uniform_int_distribution<size_t> dist{0, alpha.size() - 1};
	for (auto &c: buf)
	{
		c = alpha[dist(rng)];
	}
}

TEMPLATE_TEST_CASE(
	"codec", "[!benchmark]", pal::base64_encoder, pal::base64_decoder, pal::hex_encoder, pal::hex_decoder
)
{
	auto size = size_t{1} << GENERATE(range(2, 10));

	constexpr size_t max_src = 512;
	std::array<char, max_src> src{};
	std::array<char, TestType::max_size(max_src)> dst{};

	BENCHMARK_ADVANCED("convert/" + std::to_string(size))(auto meter)
	{
		if constexpr (!codec_alphabet<TestType>.empty())
		{
			fill_random(std::span{src}.first(size), codec_alphabet<TestType>);
		}
		else
		{
			fill_random(std::span{src}.first(size));
		}
		meter.measure([&] { return pal::convert(TestType{}, dst, std::string_view{src.data(), size}); });
	};
}

} // namespace
