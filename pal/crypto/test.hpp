#pragma once

/**
 * \file pal/crypto/test.hpp
 * Test infrastructure for pal/crypto certificate tests
 */

#include <pal/crypto/certificate.hpp>
#include <pal/crypto/certificate_store.hpp>
#include <pal/crypto/key.hpp>
#include <pal/test.hpp>
#include <pal/codec.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

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
	pal::crypto::key_algorithm algorithm;
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

/// Load a certificate from \a info's PEM text.
inline auto load_pem (const info &i)
{
	auto c = pal::crypto::certificate::from_pem(i.pem);
	REQUIRE(c);
	return std::move(*c);
}

/// Load a full certificate chain from a PKCS#12 blob.
inline auto load_pkcs12 (const auto &pkcs12)
{
	auto store = pal::crypto::certificate_store::from_pkcs12(pkcs12);
	REQUIRE(store);
	return std::ranges::to<std::vector<pal::crypto::certificate>>(*store);
}

} // namespace pal_test::cert

#include <pal/crypto/test_certs.hpp>
#include <pal/crypto/test_pkcs12.hpp>
