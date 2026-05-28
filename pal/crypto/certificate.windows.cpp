#include <pal/crypto/__crypto.hpp>

#if __pal_crypto_windows

// clang-format off
#include <pal/crypto/__certificate.hpp>
#include <pal/codec.hpp>
#include <pal/memory.hpp>
#include <pal/net/ip/address_v4.hpp>
#include <pal/net/ip/address_v6.hpp>
#include <algorithm>
#include <ctime>
#include <ncrypt.h>
// clang-format on

namespace pal::crypto
{

namespace
{

certificate::time_type to_time (const FILETIME &time) noexcept
{
	std::tm tm{};
	SYSTEMTIME sys_time;
	if (::FileTimeToSystemTime(&time, &sys_time))
	{
		tm.tm_sec = sys_time.wSecond;
		tm.tm_min = sys_time.wMinute;
		tm.tm_hour = sys_time.wHour;
		tm.tm_mday = sys_time.wDay;
		tm.tm_mon = sys_time.wMonth - 1;
		tm.tm_year = sys_time.wYear - 1900;
	}
	return certificate::clock_type::from_time_t(std::mktime(&tm));
}

int dn_count (const ::CERT_NAME_BLOB &blob) noexcept
{
	const asn_decoder<::CERT_NAME_INFO, 2048> decoder{X509_NAME, blob.pbData, blob.cbData};
	return decoder.is_valid ? static_cast<int>(decoder.value.cRDN) : 0;
}

} // namespace

distinguished_name::impl_type::impl_type (::CERT_NAME_BLOB b) noexcept
	: name{b}
	, count{dn_count(b)}
{
}

certificate::impl_type::impl_type (cert_ptr x509) noexcept
	: x509{std::move(x509)}
	, bytes{reinterpret_cast<const std::byte *>(this->x509->pbCertEncoded), this->x509->cbCertEncoded}
	, serial_number{init_serial_number()}
	, common_name{init_common_name()}
	, fingerprint{init_fingerprint()}
	, not_before{to_time(this->x509->pCertInfo->NotBefore)}
	, not_after{to_time(this->x509->pCertInfo->NotAfter)}
	, subject_dn{this->x509->pCertInfo->Subject}
	, issuer_dn{this->x509->pCertInfo->Issuer}
	, subject_san_value{init_san_value()}
{
}

certificate::impl_type::~impl_type () noexcept
{
	if (private_key)
	{
		// Remove the persisted key from disk; see open_pfx in certificate_store.windows.cpp.
		::NCryptDeleteKey(private_key, NCRYPT_SILENT_FLAG);
		::NCryptFreeObject(private_key);
	}
}

std::span<const uint8_t> certificate::impl_type::init_serial_number () noexcept
{
	const auto *first = x509->pCertInfo->SerialNumber.pbData;
	const auto *last = first + x509->pCertInfo->SerialNumber.cbData;
	while (last != first && last[-1] == 0)
	{
		--last;
	}
	return {serial_number_buf.begin(), std::reverse_copy(first, last, serial_number_buf.begin())};
}

std::string_view certificate::impl_type::init_common_name () noexcept
{
	static char oid[] = szOID_COMMON_NAME;
	::CertGetNameString(
		x509.get(),
		CERT_NAME_ATTR_TYPE,
		0,
		oid,
		common_name_buf.data(),
		static_cast<DWORD>(common_name_buf.size())
	);
	return common_name_buf.data();
}

alternative_name_value certificate::impl_type::init_san_value () const noexcept
{
	alternative_name_value result{};

	const auto &info = *x509->pCertInfo;
	const auto *ext = ::CertFindExtension(szOID_SUBJECT_ALT_NAME2, info.cExtension, info.rgExtension);
	if (!ext)
	{
		return result;
	}

	// clang-format off

	const asn_decoder<::CERT_ALT_NAME_INFO, 3 * 1024> decoder{X509_ALTERNATE_NAME, ext->Value.pbData, ext->Value.cbData};
	if (!decoder.is_valid)
	{
		return result;
	}

	std::array<char, 256> buf{};
	const auto to_dns_view = [&buf] (::LPCWSTR wide) noexcept -> std::string_view
	{
		const auto len = ::WideCharToMultiByte(
			CP_UTF8,
			0,
			wide,
			-1,
			buf.data(),
			static_cast<int>(buf.size()),
			nullptr,
			nullptr
		);
		return len > 0 ? std::string_view{buf.data(), static_cast<size_t>(len - 1)} : std::string_view{};
	};

	const auto to_ip_view = [&buf] (const ::CRYPT_DATA_BLOB &ip) noexcept -> std::string_view
	{
		const char *end = nullptr;
		if (ip.cbData == sizeof(net::ip::address_v4::bytes_type))
		{
			end = net::ip::__address_v4::ntop(ip.pbData, buf.data(), buf.data() + buf.size());
		}
		else if (ip.cbData == sizeof(net::ip::address_v6::bytes_type))
		{
			end = net::ip::__address_v6::ntop(ip.pbData, buf.data(), buf.data() + buf.size());
		}
		if (end != nullptr)
		{
			return std::string_view{buf.data(), static_cast<size_t>(end - buf.data())};
		}
		return {};
	};

	// clang-format on

	auto *p = result.data_.data();
	for (auto i = 0u; i < decoder.value.cAltEntry && p != nullptr; ++i)
	{
		const auto &alt = decoder.value.rgAltEntry[i];
		if (alt.dwAltNameChoice == CERT_ALT_NAME_DNS_NAME)
		{
			if (const auto sv = to_dns_view(alt.pwszDNSName); !sv.empty())
			{
				p = result.append(p, alternative_name_value::kind::fqdn, sv);
			}
		}
		else if (alt.dwAltNameChoice == CERT_ALT_NAME_IP_ADDRESS)
		{
			if (const auto sv = to_ip_view(alt.IPAddress); !sv.empty())
			{
				p = result.append(p, alternative_name_value::kind::ip, sv);
			}
		}
	}

	return result;
}

std::string_view certificate::impl_type::init_fingerprint () noexcept
{
	std::array<BYTE, sha1_hex_size / 2> hash{};
	auto hash_size = static_cast<DWORD>(hash.size());
	::CertGetCertificateContextProperty(x509.get(), CERT_SHA1_HASH_PROP_ID, hash.data(), &hash_size);
	convert(hex_encode, fingerprint_buf, std::as_bytes(std::span{hash}));
	return {fingerprint_buf.data(), fingerprint_buf.size()};
}

result<certificate> certificate::import_der (std::span<const std::byte> der) noexcept
{
	auto data = reinterpret_cast<const BYTE *>(der.data());
	auto size = static_cast<DWORD>(der.size());
	if (cert_ptr x509{::CertCreateCertificateContext(X509_ASN_ENCODING, data, size)})
	{
		return pal::make_shared<impl_type>(std::move(x509)).transform(certificate::to_api);
	}
	return make_unexpected(std::errc::invalid_argument);
}

std::span<const std::byte> certificate::as_bytes () const noexcept
{
	return impl_->bytes;
}

int certificate::version () const noexcept
{
	return static_cast<int>(impl_->x509->pCertInfo->dwVersion) + 1;
}

std::span<const uint8_t> certificate::serial_number () const noexcept
{
	return impl_->serial_number;
}

std::string_view certificate::common_name () const noexcept
{
	return impl_->common_name;
}

std::string_view certificate::fingerprint () const noexcept
{
	return impl_->fingerprint;
}

certificate::time_type certificate::not_before () const noexcept
{
	return impl_->not_before;
}

certificate::time_type certificate::not_after () const noexcept
{
	return impl_->not_after;
}

bool certificate::is_issued_by (const certificate &that) const noexcept
{
	return ::CertCompareCertificateName(
		X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
		&that.impl_->x509->pCertInfo->Subject,
		&impl_->x509->pCertInfo->Issuer
	);
}

bool certificate::is_self_signed () const noexcept
{
	return ::CertCompareCertificateName(
		X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
		&impl_->x509->pCertInfo->Subject,
		&impl_->x509->pCertInfo->Issuer
	);
}

namespace
{

const ::CERT_EXTENSION *get_alternative_name (const ::CERT_CONTEXT *x509, ::LPCSTR oid) noexcept
{
	const auto &info = *x509->pCertInfo;
	return ::CertFindExtension(oid, info.cExtension, info.rgExtension);
}

} // namespace

result<alternative_name> certificate::subject_alternative_name () const noexcept
{
	if (const auto *ext = get_alternative_name(impl_->x509.get(), szOID_SUBJECT_ALT_NAME2))
	{
		return pal::make_shared<alternative_name::impl_type>(impl_, *ext).transform(alternative_name::to_api);
	}
	return alternative_name{nullptr};
}

result<alternative_name> certificate::issuer_alternative_name () const noexcept
{
	if (const auto *ext = get_alternative_name(impl_->x509.get(), szOID_ISSUER_ALT_NAME2))
	{
		return pal::make_shared<alternative_name::impl_type>(impl_, *ext).transform(alternative_name::to_api);
	}
	return alternative_name{nullptr};
}

result<key> certificate::public_key () const noexcept
{
	::BCRYPT_KEY_HANDLE pkey = {};

	// clang-format off

	auto status = ::CryptImportPublicKeyInfoEx2(
		X509_ASN_ENCODING,
		&impl_->x509->pCertInfo->SubjectPublicKeyInfo,
		0,
		nullptr,
		&pkey
	);

	if (status)
	{
		return pal::make_shared<key::impl_type>(impl_, pkey)
			.transform(key::to_api)
			.or_else([&pkey] (std::error_code ec) -> result<key>
			{
				::BCryptDestroyKey(pkey);
				return pal::unexpected{std::move(ec)};
			});
	}

	// clang-format on

	return make_unexpected(std::errc::invalid_argument);
}

result<key> certificate::private_key () const noexcept
{
	if (impl_->private_key)
	{
		return pal::make_shared<key::impl_type>(impl_, impl_->private_key).transform(key::to_api);
	}
	return make_unexpected(std::errc::io_error);
}

} // namespace pal::crypto

#endif // __pal_crypto_windows
