#include <pal/codec.hpp>
#include <pal/test.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <algorithm>
#include <array>
#include <span>
#include <string_view>

namespace
{

struct success_case
{
	std::string_view decoded, encoded;
};
struct bad_size_case
{
	std::string_view encoded;
};
struct bad_input_case
{
	std::string_view encoded;
};

template <typename C>
struct cases;

template <>
struct cases<pal::base64_encoder>
{
	static constexpr success_case success[] = {
		{.decoded = "", .encoded = ""},
		{.decoded = "f", .encoded = "Zg=="},
		{.decoded = "fo", .encoded = "Zm8="},
		{.decoded = "foo", .encoded = "Zm9v"},
		{.decoded = "foob", .encoded = "Zm9vYg=="},
		{.decoded = "fooba", .encoded = "Zm9vYmE="},
		{.decoded = "foobar", .encoded = "Zm9vYmFy"},
	};

	static constexpr bad_size_case bad_size[] = {
		{"Z"},
		{"Zg"},
		{"Zg="},
		{"Zm9v="},
		{"Zm9v=="},
	};

	static constexpr bad_input_case bad_input[] = {
		// 'test' -> 'dGVzdA=='
		{".GVzdA=="},
		{"d.VzdA=="},
		{"dG.zdA=="},
		{"dGV.dA=="},
		{"dGVz.A=="},
		{"dGVzd.=="},
		{"dGVzdA.="},
		{"dGVzdA=."},

		// 'test1' -> 'dGVzdDE='
		{".GVzdDE="},
		{"d.VzdDE="},
		{"dG.zdDE="},
		{"dGV.dDE="},
		{"dGVz.DE="},
		{"dGVzd.E="},
		{"dGVzdD.="},
		{"dGVzdDE."},

		// 'test12' -> 'dGVzdDEx'
		{".GVzdDEx"},
		{"d.VzdDEx"},
		{"dG.zdDEx"},
		{"dGV.dDEx"},
		{"dGVz.DEx"},
		{"dGVzd.Ex"},
		{"dGVzdD.x"},
		{"dGVzdDE."},
	};
};

template <>
struct cases<pal::hex_encoder>
{
	static constexpr success_case success[] = {
		{.decoded = "", .encoded = ""},
		{.decoded = "hex_string", .encoded = "6865785f737472696e67"},
		{.decoded = "HEX_STRING", .encoded = "4845585f535452494e47"},
		{.decoded = "hex\nstring", .encoded = "6865780a737472696e67"},
		{.decoded = "hello, world", .encoded = "68656c6c6f2c20776f726c64"},
	};

	static constexpr bad_size_case bad_size[] = {
		{"7"},
		{"746"},
	};

	static constexpr bad_input_case bad_input[] = {
		{"7"},
		{"746"},
		{"74657"},
		{"7465737"},
		{".4657374"},
		{"7.657374"},
		{"74.57374"},
		{"746.7374"},
		{"7465.374"},
		{"74657.74"},
		{"746573.4"},
		{"7465737."},
	};
};

template <typename Encoder, typename Decoder>
struct codec
{
	using encoder = Encoder;
	using decoder = Decoder;
};

using base64 = codec<pal::base64_encoder, pal::base64_decoder>;
using hex = codec<pal::hex_encoder, pal::hex_decoder>;

TEMPLATE_TEST_CASE("codec", "", base64, hex)
{
	const typename TestType::encoder encoder{};
	const typename TestType::decoder decoder{};

	using Cases = cases<typename TestType::encoder>;
	char buf[1024];

	SECTION("constexpr")
	{
		constexpr auto &s = Cases::success[1];
		static_assert(pal::convert_max_size(encoder, s.decoded) == s.encoded.size());
		static_assert(pal::convert_max_size(decoder, s.encoded) >= s.decoded.size());
	}

	SECTION("convert_max_size/pointer-pair")
	{
		const auto &[d, e] = Cases::success[1];
		CHECK(pal::convert_max_size(encoder, d.data(), d.data() + d.size()) == e.size());
		CHECK(pal::convert_max_size(decoder, e.data(), e.data() + e.size()) >= d.size());
	}

	SECTION("convert_max_size/range")
	{
		const auto &[d, e] = Cases::success[1];
		CHECK(pal::convert_max_size(encoder, d) == e.size());
		CHECK(pal::convert_max_size(decoder, e) >= d.size());
	}

	SECTION("encoder/success")
	{
		const auto &[decoded, encoded] = GENERATE(from_range(std::span{Cases::success}));
		CAPTURE(decoded, encoded);
		auto [ptr, ec] = pal::convert(encoder, buf, decoded);
		REQUIRE(ec == std::errc{});
		CHECK(std::string_view{buf, ptr} == encoded);
	}

	SECTION("encoder/value_too_large")
	{
		const auto &[decoded, encoded] = Cases::success[1];
		CAPTURE(decoded, encoded);
		auto [ptr, ec] = pal::convert(encoder, buf, buf, decoded.data(), decoded.data() + decoded.size());
		CHECK(ec == std::errc::value_too_large);
		CHECK(ptr == buf);
	}

	SECTION("decoder/success")
	{
		const auto &[decoded, encoded] = GENERATE(from_range(std::span{Cases::success}));
		CAPTURE(decoded, encoded);
		auto [ptr, ec] = pal::convert(decoder, buf, encoded);
		REQUIRE(ec == std::errc{});
		CHECK(std::string_view{buf, ptr} == decoded);
	}

	SECTION("decoder/bad_size")
	{
		const auto &[encoded] = GENERATE(from_range(std::span{Cases::bad_size}));
		CAPTURE(encoded);
		auto [ptr, ec] = pal::convert(decoder, buf, encoded);
		CHECK(ec == std::errc::illegal_byte_sequence);
		CHECK(ptr == buf + sizeof(buf));
	}

	SECTION("decoder/bad_input")
	{
		const auto &[encoded] = GENERATE(from_range(std::span{Cases::bad_input}));
		CAPTURE(encoded);
		auto [ptr, ec] = pal::convert(decoder, buf, encoded);
		CHECK(ec == std::errc::illegal_byte_sequence);
		CHECK(ptr == buf + sizeof(buf));
	}

	SECTION("decoder/value_too_large")
	{
		const auto &[decoded, encoded] = Cases::success[1];
		CAPTURE(decoded, encoded);
		auto [ptr, ec] = pal::convert(decoder, buf, buf, encoded.data(), encoded.data() + encoded.size());
		CHECK(ec == std::errc::value_too_large);
		CHECK(ptr == buf);
	}

	SECTION("binary/roundtrip")
	{
		constexpr size_t n = 256;
		std::array<std::byte, n> src{};
		for (size_t i = 0; i < n; ++i)
		{
			src[i] = std::byte{static_cast<uint8_t>(i)};
		}

		std::array<char, TestType::encoder::max_size(n)> encoded{};
		auto [enc_ptr, enc_ec] = pal::convert(encoder, encoded, src);
		REQUIRE(enc_ec == std::errc{});
		CHECK(enc_ptr == encoded.data() + TestType::encoder::max_size(n));

		constexpr auto dst_max = TestType::decoder::max_size(TestType::encoder::max_size(n));
		std::array<std::byte, dst_max> dst{};
		auto [ptr, ec] = pal::convert(decoder, dst.data(), dst.data() + dst.size(), encoded.data(), enc_ptr);
		REQUIRE(ec == std::errc{});
		REQUIRE(ptr == dst.data() + n);
		CHECK(std::equal(dst.begin(), dst.begin() + n, src.begin(), src.end()));
	}
}

} // namespace
