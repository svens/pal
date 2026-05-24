#include <pal/crypto/__crypto.hpp>

#if __pal_crypto_windows

// clang-format off
#include <pal/crypto/__certificate.hpp>
#include <pal/codec.hpp>
#include <pal/memory.hpp>
#include <algorithm>
#include <ctime>
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

} // namespace

certificate::impl_type::impl_type (cert_ptr x509) noexcept
	: x509{std::move(x509)}
	, bytes{reinterpret_cast<const std::byte *>(this->x509->pbCertEncoded), this->x509->cbCertEncoded}
	, serial_number{init_serial_number()}
	, common_name{init_common_name()}
	, fingerprint{init_fingerprint()}
	, not_before{to_time(this->x509->pCertInfo->NotBefore)}
	, not_after{to_time(this->x509->pCertInfo->NotAfter)}
{
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
		return pal::make_shared<impl_type>(std::move(x509)).transform(impl_type::to_api);
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

} // namespace pal::crypto

#endif // __pal_crypto_windows
