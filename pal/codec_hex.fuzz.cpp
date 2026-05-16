#include <pal/codec.hpp>
#include <pal/fuzz.hpp>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string_view>
#include <tuple>

namespace
{

constexpr size_t max_input = 4096;
constexpr size_t max_encoded = pal::hex_encoder::max_size(max_input);
constexpr size_t max_decoded = pal::hex_decoder::max_size(max_input + max_input / 2);

} // namespace

extern "C" int LLVMFuzzerInitialize (int *argc, char ***argv)
{
	pal::fuzz::init(argc, argv, max_input);
	return 0;
}

extern "C" int LLVMFuzzerTestOneInput (const uint8_t *data, std::size_t size)
{
	const auto blob = std::string_view{reinterpret_cast<const char *>(data), size};
	std::array<char, max_decoded> decoded{};

	//
	// robustness: arbitrary input to decoder must not crash
	//

	std::ignore = pal::convert(pal::hex_decode, decoded, blob);

	//
	// roundtrip: decode(encode(x)) == x for any binary input
	//

	std::array<char, max_encoded> encoded{};
	auto [enc_ptr, enc_ec] = pal::convert(pal::hex_encode, encoded, blob);
	if (enc_ec != std::errc{})
	{
		return 0;
	}

	auto [dec_ptr, dec_ec] = pal::convert(pal::hex_decode, decoded, std::string_view{encoded.data(), enc_ptr});
	if (dec_ec != std::errc{})
	{
		std::abort();
	}
	if (dec_ptr - decoded.data() != static_cast<std::ptrdiff_t>(size))
	{
		std::abort();
	}
	if (!std::equal(decoded.data(), decoded.data() + size, blob.begin()))
	{
		std::abort();
	}

	return 0;
}
