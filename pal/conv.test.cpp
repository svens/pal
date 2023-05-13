#include <pal/conv>
#include <pal/test>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <array>
#include <span>
#include <string_view>

namespace {

using pal::base64;
using pal::hex;

struct success_type
{
	std::string_view decoded, encoded;
};

struct size_failure_type
{
	std::string_view encoded;
};

struct decode_failure_type
{
	std::string_view encoded;
};

template <typename Algorithm> //{{{1
struct test_data;

template <>
struct test_data<base64> //{{{1
{
	static inline constexpr success_type success[] =
	{
		{ "", "" },
		{ "f", "Zg==" },
		{ "fo", "Zm8=" },
		{ "foo", "Zm9v" },
		{ "foob", "Zm9vYg==" },
		{ "fooba", "Zm9vYmE=" },
		{ "foobar", "Zm9vYmFy" },
	};

	static inline constexpr size_failure_type size_failure[] =
	{
		{ "Z" },
		{ "Zg" },
		{ "Zg=" },
		{ "Zm9v=" },
		{ "Zm9v==" },
	};

	static inline constexpr decode_failure_type decode_failure[] =
	{
		{ "Z" },
		{ "Zg" },
		{ "Zg=" },
		{ "Zm9v=" },
		{ "Zm9v==" },

		// 'test' -> 'dGVzdA=='
		{ ".GVzdA==" },
		{ "d.VzdA==" },
		{ "dG.zdA==" },
		{ "dGV.dA==" },
		{ "dGVz.A==" },
		{ "dGVzd.==" },
		{ "dGVzdA.=" },
		{ "dGVzdA=." },

		// 'test1' -> 'dGVzdDE='
		{ ".GVzdDE=" },
		{ "d.VzdDE=" },
		{ "dG.zdDE=" },
		{ "dGV.dDE=" },
		{ "dGVz.DE=" },
		{ "dGVzd.E=" },
		{ "dGVzdD.=" },
		{ "dGVzdDE." },

		// 'test12' -> 'dGVzdDEx'
		{ ".GVzdDEx" },
		{ "d.VzdDEx" },
		{ "dG.zdDEx" },
		{ "dGV.dDEx" },
		{ "dGVz.DEx" },
		{ "dGVzd.Ex" },
		{ "dGVzdD.x" },
		{ "dGVzdDE." },
	};
};

template <>
struct test_data<hex> //{{{1
{
	static inline constexpr success_type success[] =
	{
		{"", ""},
		{"hex_string", "6865785f737472696e67"},
		{"HEX_STRING", "4845585f535452494e47"},
		{"hex\nstring", "6865780a737472696e67"},
		{"hello, world", "68656c6c6f2c20776f726c64"},
	};

	static inline constexpr size_failure_type size_failure[] =
	{
		{ "7" },
		{ "746" },
	};

	static inline constexpr decode_failure_type decode_failure[] =
	{
		{ "7" },
		{ "746" },
		{ "74657" },
		{ "7465737" },
		{ ".4657374" },
		{ "7.657374" },
		{ "74.57374" },
		{ "746.7374" },
		{ "7465.374" },
		{ "74657.74" },
		{ "746573.4" },
		{ "7465737." },
	};
};

//}}}1

constexpr auto generate_extended_ascii_table () noexcept
{
	std::array<std::byte, 256> table{};
	for (uint16_t v = 0;  v < table.size();  ++v)
	{
		table[v] = std::byte{static_cast<uint8_t>(v)};
	}
	return table;
}
constexpr const auto extended_ascii = generate_extended_ascii_table();

TEMPLATE_TEST_CASE("conv", "",
	base64,
	hex)
{
	using TestData = test_data<TestType>;
	char buf[1024];

	SECTION("constexpr")
	{
		constexpr auto &success = TestData::success[1];
		static_assert(pal::decode_size<TestType>(success.encoded) >= success.decoded.size());

		constexpr auto &failure = TestData::size_failure[0];
		static_assert(pal::decode_size<TestType>(failure.encoded) == -1);
	}

	SECTION("success")
	{
		const auto &[decoded, encoded] = GENERATE(from_range(std::span{TestData::success}));
		CAPTURE(decoded, encoded);

		SECTION("encode")
		{
			CHECK(pal::encode_size<TestType>(decoded) == encoded.size());
			CHECK(std::string_view{buf, pal::encode<TestType>(decoded, buf)} == encoded);
		}

		SECTION("decode")
		{
			CHECK(pal::decode_size<TestType>(encoded) >= decoded.size());
			CHECK(std::string_view{buf, pal::decode<TestType>(encoded, buf)} == decoded);
		}
	}

	SECTION("failure")
	{
		SECTION("size")
		{
			const auto &[encoded] = GENERATE(from_range(std::span{TestData::size_failure}));
			CAPTURE(encoded);
			CHECK(pal::decode_size<TestType>(encoded) == (size_t)-1);
		}

		SECTION("decode")
		{
			const auto &[encoded] = GENERATE(from_range(std::span{TestData::decode_failure}));
			CAPTURE(encoded);
			CHECK(pal::decode<TestType>(encoded, buf) == nullptr);
		}
	}

	SECTION("binary")
	{
		std::string encoded{buf, pal::encode<TestType>(extended_ascii, buf)};
		REQUIRE(encoded.size() == pal::encode_size<TestType>(extended_ascii));

		std::remove_const_t<decltype(extended_ascii)> decoded{};
		REQUIRE(decoded.end() == pal::decode<TestType>(encoded, decoded.begin()));

		CHECK(decoded == extended_ascii);
	}
}

} // namespace
