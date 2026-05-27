#pragma once

/**
 * \file pal/crypto/__certificate.hpp
 * Platform certificate implementation types (internal)
 *
 * \internal distinguished_name::impl_type is embedded by value inside
 * certificate::impl_type rather than holding a certificate::impl_ptr owner
 * like other property types. This allows subject_name()/issuer_name() to use
 * the shared_ptr aliasing constructor — zero extra allocation. DN is always
 * present in a valid certificate and always accessed, making eager embedding
 * the right tradeoff. Optional/lazy properties (alternative_name, key) use
 * the owner pattern instead.
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

struct general_names_deleter
{
	void operator() (::GENERAL_NAMES *p) const noexcept
	{
		sk_GENERAL_NAME_pop_free(p, GENERAL_NAME_free);
	}
};
using general_names_ptr = std::unique_ptr<::GENERAL_NAMES, general_names_deleter>;

struct cert_deleter
{
	void operator() (::X509 *p) const noexcept
	{
		::X509_free(p);
	}
};
using cert_ptr = std::unique_ptr<::X509, cert_deleter>;

struct distinguished_name::impl_type
{
	::X509_NAME *name;
	const int count;

	explicit impl_type (::X509_NAME *n) noexcept
		: name{n}
		, count{::X509_NAME_entry_count(n)}
	{
	}

	bool load_at (int index, oid_buffer &oid, value_buffer &value, entry &e) const noexcept;
};

struct alternative_name::impl_type
{
	certificate::impl_ptr owner;
	general_names_ptr names;
	const int count;

	explicit impl_type (certificate::impl_ptr owner, general_names_ptr names) noexcept
		: owner{std::move(owner)}
		, names{std::move(names)}
		, count{sk_GENERAL_NAME_num(this->names.get())}
	{
	}

	bool load_at (int index, entry_buffer &buf, alternative_name_entry &entry) const noexcept;
};

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

	distinguished_name::impl_type subject_dn;
	distinguished_name::impl_type issuer_dn;
	alternative_name_value subject_san_value;

	impl_type (cert_ptr x509, std::span<const std::byte> der);
	impl_type (const impl_type &) = delete;
	impl_type &operator= (const impl_type &) = delete;

private:

	std::span<const std::byte> init_bytes (std::span<const std::byte> der) noexcept;
	[[nodiscard]] std::span<const uint8_t> init_serial_number () const noexcept;
	[[nodiscard]] std::string_view init_common_name () const noexcept;
	std::string_view init_fingerprint () noexcept;
	[[nodiscard]] alternative_name_value init_san_value () const noexcept;
};

#elif __pal_crypto_windows //{{{1

template <typename T, size_t Size>
struct asn_decoder
{
	temporary_buffer<Size> buf;
	const bool is_valid;
	const T &value;

	asn_decoder (::LPCSTR type, const ::BYTE *data, ::DWORD size) noexcept
		: buf{std::nothrow, decode_size(type, data, size)}
		, is_valid{decode(type, data, size, buf)}
		, value{*reinterpret_cast<const T *>(buf.get().data())}
	{
	}

private:

	static constexpr ::DWORD decode_content = X509_ASN_ENCODING;
	static constexpr ::DWORD decode_flags = CRYPT_DECODE_NOCOPY_FLAG | CRYPT_DECODE_SHARE_OID_STRING_FLAG;

	static ::DWORD decode_size (::LPCSTR type, const ::BYTE *data, ::DWORD size) noexcept
	{
		::DWORD needed = 0;
		::CryptDecodeObjectEx(decode_content, type, data, size, decode_flags, nullptr, nullptr, &needed);
		return needed;
	}

	static bool decode (::LPCSTR type, const ::BYTE *data, ::DWORD size, temporary_buffer<Size> &buf) noexcept
	{
		auto ptr = buf.get().data();
		auto len = static_cast<::DWORD>(buf.get().size());
		return ::CryptDecodeObjectEx(decode_content, type, data, size, decode_flags, nullptr, ptr, &len);
	}
};

struct cert_deleter
{
	void operator() (const ::CERT_CONTEXT *p) const noexcept
	{
		::CertFreeCertificateContext(p);
	}
};
using cert_ptr = std::unique_ptr<const ::CERT_CONTEXT, cert_deleter>;

struct distinguished_name::impl_type
{
	::CERT_NAME_BLOB name;
	const int count;

	explicit impl_type (::CERT_NAME_BLOB b) noexcept;

	bool load_at (int index, oid_buffer &oid, value_buffer &value, entry &e) const noexcept;
};

struct alternative_name::impl_type
{
	certificate::impl_ptr owner;
	asn_decoder<::CERT_ALT_NAME_INFO, 3 * 1024> name;
	const int count;

	explicit impl_type (certificate::impl_ptr owner, const ::CERT_EXTENSION &ext) noexcept
		: owner{std::move(owner)}
		, name{X509_ALTERNATE_NAME, ext.Value.pbData, ext.Value.cbData}
		, count{name.is_valid ? static_cast<int>(name.value.cAltEntry) : 0}
	{
	}

	bool load_at (int index, entry_buffer &buf, alternative_name_entry &entry) const noexcept;
};

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

	distinguished_name::impl_type subject_dn;
	distinguished_name::impl_type issuer_dn;
	alternative_name_value subject_san_value;

	explicit impl_type (cert_ptr x509) noexcept;
	impl_type (const impl_type &) = delete;
	impl_type &operator= (const impl_type &) = delete;

private:

	std::span<const uint8_t> init_serial_number () noexcept;
	std::string_view init_common_name () noexcept;
	std::string_view init_fingerprint () noexcept;
	[[nodiscard]] alternative_name_value init_san_value () const noexcept;
};

#endif //}}}1

} // namespace pal::crypto
