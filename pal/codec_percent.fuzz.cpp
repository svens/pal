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
constexpr size_t max_encoded = pal::percent_encoder::max_size(max_input);
constexpr size_t max_decoded = pal::percent_decoder::max_size(max_encoded);

constexpr bool is_unreserved (char c) noexcept
{
	return (c >= 'A' && c <= 'Z')
		|| (c >= 'a' && c <= 'z')
		|| (c >= '0' && c <= '9')
		|| c == '-'
		|| c == '.'
		|| c == '_'
		|| c == '~'
	;
}

constexpr bool is_upper_hex (char c) noexcept
{
	return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F');
}

bool is_canonical (std::string_view encoded) noexcept
{
	for (size_t i = 0; i < encoded.size(); ++i)
	{
		if (encoded[i] == '%')
		{
			if (i + 2 >= encoded.size() || !is_upper_hex(encoded[i + 1]) || !is_upper_hex(encoded[i + 2]))
			{
				return false;
			}
			i += 2;
		}
		else if (!is_unreserved(encoded[i]))
		{
			return false;
		}
	}
	return true;
}

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

	std::ignore = pal::convert(pal::percent_decode, decoded, blob);

	std::array<char, max_encoded> encoded{};
	auto [enc_ptr, enc_ec] = pal::convert(pal::percent_encode, encoded, blob);
	if (enc_ec != std::errc{})
	{
		return 0;
	}

	const auto encoded_text = std::string_view{encoded.data(), enc_ptr};
	if (!is_canonical(encoded_text))
	{
		std::abort();
	}

	auto [dec_ptr, dec_ec] = pal::convert(pal::percent_decode, decoded, encoded_text);
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
