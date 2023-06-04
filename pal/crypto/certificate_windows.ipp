#include <pal/crypto/hash>
#include <wincrypt.h>

namespace pal::crypto {

struct certificate::impl_type
{
	using x509_ptr = std::unique_ptr<const ::CERT_CONTEXT, decltype(&::CertFreeCertificateContext)>;
	x509_ptr x509;

	char common_name_buf[512 + 1];
	std::string_view common_name;

	char fingerprint_buf[hex::encode_size(sha1_hash::digest_size) + 1];
	std::string_view fingerprint;

	impl_type (x509_ptr &&x509) noexcept
		: x509{std::move(x509)}
		, common_name{init_common_name()}
		, fingerprint{init_fingerprint()}
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

} // namespace pal::crypto
