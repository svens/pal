#pragma once

/**
 * \file pal/crypto/__certificate.hpp
 * Platform certificate implementation types (internal)
 */

#include <pal/crypto/__crypto.hpp>
#include <pal/crypto/certificate.hpp>
#include <pal/memory.hpp>
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

#if __pal_crypto_openssl
	#include <openssl/x509.h>
	#include <openssl/x509v3.h>
#elif __pal_crypto_windows
	#include <wincrypt.h>
#endif

namespace pal::crypto
{

#if __pal_crypto_openssl //{{{1

struct cert_deleter
{
	void operator() (::X509 *p) const noexcept
	{
		::X509_free(p);
	}
};
using cert_ptr = std::unique_ptr<::X509, cert_deleter>;

struct certificate::impl_type
{
	cert_ptr x509;

	static constexpr size_t bytes_stack_size = 2048;
	temporary_buffer<bytes_stack_size> bytes_buf;
	std::span<const std::byte> bytes;

	std::span<const uint8_t> serial_number;

	std::string_view common_name;

	static constexpr size_t sha1_hex_size = 40;
	std::array<char, sha1_hex_size> fingerprint_buf;
	std::string_view fingerprint;

	certificate::time_type not_before;
	certificate::time_type not_after;

	impl_type (cert_ptr x509, std::span<const std::byte> der);
	impl_type (const impl_type &) = delete;
	impl_type &operator= (const impl_type &) = delete;

	static certificate to_api (certificate::impl_ptr impl) noexcept;

private:

	std::span<const std::byte> init_bytes (std::span<const std::byte> der) noexcept;
	[[nodiscard]] std::span<const uint8_t> init_serial_number () const noexcept;
	[[nodiscard]] std::string_view init_common_name () const noexcept;
	std::string_view init_fingerprint () noexcept;
};

#elif __pal_crypto_windows //{{{1

struct cert_deleter
{
	void operator() (const ::CERT_CONTEXT *p) const noexcept
	{
		::CertFreeCertificateContext(p);
	}
};
using cert_ptr = std::unique_ptr<const ::CERT_CONTEXT, cert_deleter>;

struct certificate::impl_type
{
	cert_ptr x509;

	std::span<const std::byte> bytes;

	static constexpr size_t max_serial_bytes = 20;
	std::array<uint8_t, max_serial_bytes> serial_number_buf;
	std::span<const uint8_t> serial_number;

	static constexpr size_t common_name_max = 64;
	std::array<char, common_name_max> common_name_buf{};
	std::string_view common_name;

	static constexpr size_t sha1_hex_size = 40;
	std::array<char, sha1_hex_size> fingerprint_buf;
	std::string_view fingerprint;

	certificate::time_type not_before;
	certificate::time_type not_after;

	explicit impl_type (cert_ptr x509) noexcept;
	impl_type (const impl_type &) = delete;
	impl_type &operator= (const impl_type &) = delete;

	static certificate to_api (certificate::impl_ptr impl) noexcept;

private:

	std::span<const uint8_t> init_serial_number () noexcept;
	std::string_view init_common_name () noexcept;
	std::string_view init_fingerprint () noexcept;
};

#endif //}}}1

inline certificate certificate::impl_type::to_api (certificate::impl_ptr impl) noexcept
{
	return certificate{std::move(impl)};
}

} // namespace pal::crypto
