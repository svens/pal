#include <pal/crypto/hash>
#include <wincrypt.h>

namespace pal::crypto {

namespace {

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

struct certificate::impl_type
{
	using x509_ptr = std::unique_ptr<const ::CERT_CONTEXT, decltype(&::CertFreeCertificateContext)>;
	x509_ptr x509;

	char common_name_buf[512 + 1];
	std::string_view common_name;

	char fingerprint_buf[hex::encode_size(sha1_hash::digest_size) + 1];
	std::string_view fingerprint;

	time_type not_before, not_after;

	impl_type (x509_ptr &&x509) noexcept
		: x509{std::move(x509)}
		, common_name{init_common_name()}
		, fingerprint{init_fingerprint()}
		, not_before{to_time(this->x509->pCertInfo->NotBefore)}
		, not_after{to_time(this->x509->pCertInfo->NotAfter)}
	{ }

	std::string_view init_common_name () noexcept
	{
		static char oid[] = szOID_COMMON_NAME;
		::CertGetNameString(
			x509.get(),
			CERT_NAME_ATTR_TYPE,
			0,
			oid,
			common_name_buf,
			sizeof(common_name_buf)
		);
		return common_name_buf;
	}

	std::string_view init_fingerprint () noexcept
	{
		sha1_hash::digest_type buf;
		auto buf_size = static_cast<DWORD>(buf.size());
		::CertGetCertificateContextProperty(
			x509.get(),
			CERT_SHA1_HASH_PROP_ID,
			buf.data(),
			&buf_size
		);
		encode<hex>(buf, fingerprint_buf);
		fingerprint_buf[sizeof(fingerprint_buf) - 1] = '\0';
		return fingerprint_buf;
	}
};

result<certificate> certificate::import_der (std::span<const std::byte> der) noexcept
{
	impl_type::x509_ptr x509
	{
		::CertCreateCertificateContext(
			X509_ASN_ENCODING,
			reinterpret_cast<const BYTE *>(der.data()),
			static_cast<DWORD>(der.size())
		),
		&::CertFreeCertificateContext
	};

	if (x509)
	{
		if (auto impl = impl_ptr{new(std::nothrow) impl_type(std::move(x509))})
		{
			return certificate{impl};
		}
		return make_unexpected(std::errc::not_enough_memory);
	}

	return make_unexpected(std::errc::invalid_argument);
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

} // namespace pal::crypto
