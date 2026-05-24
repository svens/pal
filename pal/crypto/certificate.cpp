#include <pal/crypto/certificate.hpp>
#include <pal/codec.hpp>
#include <pal/memory.hpp>
#include <algorithm>
#include <cctype>

namespace pal::crypto
{

namespace
{

result<std::string_view> unwrap (std::string_view pem) noexcept
{
	static constexpr std::string_view header = "-----BEGIN CERTIFICATE-----\n";
	if (pem.starts_with(header))
	{
		pem.remove_prefix(header.size());
	}
	else
	{
		return make_unexpected(std::errc::invalid_argument);
	}

	static constexpr std::string_view footer = "\n-----END CERTIFICATE-----\n";
	if (auto pos = pem.find(footer); pos != pem.npos)
	{
		pem.remove_suffix(pem.size() - pos);
	}
	else
	{
		return make_unexpected(std::errc::invalid_argument);
	}

	return pem;
}

std::string_view strip_whitespace (std::string_view src, std::span<std::byte> out) noexcept
{
	auto *data = reinterpret_cast<char *>(out.data());
	const auto [_, end] = std::ranges::copy_if(src, data, [] (unsigned char c) { return !std::isspace(c); });
	return {data, static_cast<size_t>(end - data)};
}

result<std::span<const std::byte>> decode (std::string_view b64, std::span<std::byte> out) noexcept
{
	if (const auto r = convert(base64_decode, out, b64); r.ec == std::errc{})
	{
		return std::as_bytes(out.first(static_cast<size_t>(r.ptr - out.data())));
	}
	return make_unexpected(std::errc::invalid_argument);
}

} // namespace

result<certificate> certificate::import_pem (std::string_view pem) noexcept
{
	// clang-format off
	return unwrap(pem).and_then([] (std::string_view b64_view) noexcept -> result<certificate>
	{
		static constexpr size_t der_stack_size = 2048;
		static constexpr size_t b64_stack_size = (der_stack_size * 4 / 3 + 1023) / 1024 * 1024;
		const auto der_max_size = convert_max_size(base64_decode, b64_view);

		temporary_buffer<b64_stack_size> b64_buf{std::nothrow, b64_view.size()};
		temporary_buffer<der_stack_size> der_buf{std::nothrow, der_max_size};
		if (!b64_buf || !der_buf)
		{
			return make_unexpected(std::errc::not_enough_memory);
		}

		const auto b64 = strip_whitespace(b64_view, b64_buf.get());
		return decode(b64, der_buf.get()).and_then(import_der);
	});
	// clang-format on
}

} // namespace pal::crypto
