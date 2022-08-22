#include <pal/conv>
#include <pal/test>
#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <random>


namespace {


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


template <size_t AlphabetSize>
void random (std::span<char> span, const char (&alphabet)[AlphabetSize]) noexcept
{
	std::mt19937 rng;
	rng.seed(std::random_device()());
	std::uniform_int_distribution<std::mt19937::result_type> gen(0, AlphabetSize - 2);
	for (auto &v: span)
	{
		v = alphabet[gen(rng)];
	}
}


struct base64
{
	static constexpr char alphabet[] =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
		"abcdefghijklmnopqrstuvwxyz"
		"0123456789"
		"+/";

	template <typename Data>
	static constexpr size_t to_size (const Data &in) noexcept
	{
		return pal::to_base64_size(in);
	}

	static char *to_conv (std::span<char> in, char *out) noexcept
	{
		return pal::to_base64(in, out);
	}

	template <typename Data>
	static constexpr size_t from_size (const Data &in) noexcept
	{
		return pal::from_base64_size(in);
	}

	static char *from_conv (std::span<char> in, char *out) noexcept
	{
		return pal::from_base64(in, out);
	}
};


struct hex
{
	static constexpr char alphabet[] = "0123456789abcdefABCDEF";

	template <typename Data>
	static constexpr size_t to_size (const Data &in) noexcept
	{
		return pal::to_hex_size(in);
	}

	static char *to_conv (std::span<char> in, char *out) noexcept
	{
		return pal::to_hex(in, out);
	}

	template <typename Data>
	static constexpr size_t from_size (const Data &in) noexcept
	{
		return pal::from_hex_size(in);
	}

	static char *from_conv (std::span<char> in, char *out) noexcept
	{
		return pal::from_hex(in, out);
	}
};


TEMPLATE_TEST_CASE("conv", "[!benchmark]",
	base64,
	hex)
{
	size_t size = 1 << GENERATE(range(2, 10));
	char buffer[512];
	std::span in{buffer, size};
	char out[(std::max)({TestType::to_size(buffer), TestType::from_size(buffer)})];

	BENCHMARK_ADVANCED("to/" + std::to_string(size))(auto meter)
	{
		random(in);
		meter.measure([&]{ return TestType::to_conv(in, out); });
	};

	BENCHMARK_ADVANCED("from/" + std::to_string(size))(auto meter)
	{
		random(in, TestType::alphabet);
		meter.measure([&]{ return TestType::from_conv(in, out); });
	};
}


} // namespace
