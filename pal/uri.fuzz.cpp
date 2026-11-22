#include <pal/uri.hpp>
#include <pal/codec.hpp>
#include <pal/fuzz.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace
{

constexpr size_t max_input = 4096;
constexpr size_t max_percent_input = 256;

void require (bool condition)
{
	if (!condition)
	{
		std::abort();
	}
}

template <typename T>
std::string require_to_chars (const T &value)
{
	std::string formatted(value.max_size(), '\0');
	auto [ptr, ec] = value.to_chars(formatted.data(), formatted.data() + formatted.size());
	require(ec == std::errc{});
	formatted.resize(static_cast<size_t>(ptr - formatted.data()));

	std::string exact(formatted.size(), '\0');
	auto [exact_ptr, exact_ec] = value.to_chars(exact.data(), exact.data() + exact.size());
	require(exact_ec == std::errc{} && exact_ptr == exact.data() + exact.size() && exact == formatted);

	if (!formatted.empty())
	{
		std::string small(formatted.size() - 1, '\0');
		auto [small_ptr, small_ec] = value.to_chars(small.data(), small.data() + small.size());
		require(small_ec == std::errc::value_too_large && small_ptr == small.data() + small.size());
	}

	return formatted;
}

template <typename T>
std::string roundtrip (const T &value)
{
	auto formatted = require_to_chars(value);
	auto reparsed = T::make(formatted);
	require(reparsed && *reparsed == value);
	return formatted;
}

template <typename T>
bool try_roundtrip (const T &value)
{
	std::string probe(value.max_size(), '\0');
	auto [ptr, ec] = value.to_chars(probe.data(), probe.data() + probe.size());
	if (ec != std::errc{})
	{
		require(ec == std::errc::invalid_argument && ptr == probe.data() + probe.size());
		return false;
	}
	roundtrip(value);
	return true;
}

void reg_name_property (std::string_view text)
{
	const pal::uri_host host{.value = text};
	auto parsed = pal::uri_host::make(text);
	require(try_roundtrip(host) == (parsed && *parsed == host));
}

void host_roundtrip (std::string_view blob)
{
	pal::uri_host host;
	auto [ptr, ec] = host.from_chars(blob.data(), blob.data() + blob.size());
	if (ec != std::errc{})
	{
		return;
	}

	auto consumed = std::string_view{blob.data(), ptr};
	auto whole = pal::uri_host::make(consumed);
	require(whole && *whole == host);

	if (const auto *address = std::get_if<pal::net::ip::address>(&host.value))
	{
		if (consumed.starts_with('['))
		{
			consumed = consumed.substr(1, consumed.size() - 2);
		}
		auto expected = pal::net::ip::make_address(consumed);
		require(expected && *expected == *address);
	}

	roundtrip(host);
}

void uri_roundtrip (std::string_view blob)
{
	auto uri = pal::uri_view::make(blob);
	if (!uri)
	{
		return;
	}

	if (uri->host)
	{
		require(pal::uri_host::make(*uri->host).has_value());
	}

	const auto formatted = roundtrip(*uri);
	require(formatted.size() < blob.size() || formatted == blob);
}

void percent_format (std::string_view blob)
{
	blob = blob.substr(0, max_percent_input);
	std::string encoded(pal::percent_encoder::max_size(blob.size()), '\0');
	auto [ptr, ec] = pal::convert(pal::percent_encode, encoded, blob);
	require(ec == std::errc{});
	encoded.resize(static_cast<size_t>(ptr - encoded.data()));

	reg_name_property(encoded);
	roundtrip(pal::uri_view{.userinfo = encoded, .host = encoded});
	const auto path = "/" + encoded;
	roundtrip(pal::uri_view{.path = path});
	roundtrip(pal::uri_view{.scheme = "s", .path = encoded, .query = encoded, .fragment = encoded});
}

void raw_format (std::string_view blob)
{
	reg_name_property(blob);

	std::array<std::optional<std::string_view>, 6> fields{};
	for (auto &field: fields)
	{
		if (blob.empty())
		{
			break;
		}
		auto end = blob.find('\0');
		field = blob.substr(0, end);
		blob.remove_prefix(end == blob.npos ? blob.size() : end + 1);
	}

	const auto &[path, scheme, userinfo, host, query, fragment] = fields;
	try_roundtrip(pal::uri_view{
		.scheme = scheme,
		.userinfo = userinfo,
		.host = host,
		.path = path.value_or(std::string_view{}),
		.query = query,
		.fragment = fragment,
	});
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

	host_roundtrip(blob);
	uri_roundtrip(blob);
	percent_format(blob);
	raw_format(blob);

	return 0;
}
