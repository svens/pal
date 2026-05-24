#pragma once

/**
 * \file pal/crypto/test.hpp
 * Test infrastructure for pal/crypto certificate tests
 */

#include <pal/test.hpp>
#include <pal/codec.hpp>
#include <cstddef>
#include <string>
#include <string_view>

namespace pal_test::cert
{

/// Metadata describing expected properties of a test certificate.
struct info
{
	int version;
	std::string_view common_name;
	std::string_view subject_name;
	std::string_view subject_alternative_name;
	std::string_view issuer_name;
	std::string_view issuer_alternative_name;
	std::string_view serial_number;
	std::string_view fingerprint;
	size_t size_bits;
	size_t max_block_size;
	std::string_view pem;
};

/// Decode the first PEM certificate block in \a pem to DER bytes.
inline std::string to_der (std::string_view pem)
{
	constexpr std::string_view header = "-----BEGIN CERTIFICATE-----\n";
	if (pem.starts_with(header))
	{
		pem.remove_prefix(header.size());
	}

	constexpr std::string_view footer = "\n-----END CERTIFICATE-----\n";
	if (pem.ends_with(footer))
	{
		pem.remove_suffix(footer.size());
	}

	std::string b64{pem};
	std::erase_if(b64, [] (unsigned char c) { return std::isspace(c); });

	std::string der(pal::convert_max_size(pal::base64_decode, b64), '\0');
	auto result = pal::convert(pal::base64_decode, der, b64);
	der.resize(result.ptr - der.data());
	return der;
}

} // namespace pal_test::cert

#include <pal/crypto/test_certs.hpp>
#include <pal/crypto/test_pkcs12.hpp>
