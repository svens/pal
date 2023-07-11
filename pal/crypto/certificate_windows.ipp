#include <pal/crypto/hash>
#include <pal/memory>
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

	std::array<uint8_t, 20> serial_number_buf{};
	std::span<const uint8_t> serial_number;

	distinguished_name_entry_value common_name_buf;
	std::string_view common_name;

	char fingerprint_buf[hex::encode_size(sha1_hash::digest_size) + 1];
	std::string_view fingerprint;

	time_type not_before, not_after;

	impl_type (x509_ptr &&x509) noexcept
		: x509{std::move(x509)}
		, serial_number{init_serial_number()}
		, common_name{init_common_name()}
		, fingerprint{init_fingerprint()}
		, not_before{to_time(this->x509->pCertInfo->NotBefore)}
		, not_after{to_time(this->x509->pCertInfo->NotAfter)}
	{ }

	std::span<const uint8_t> init_serial_number () noexcept
	{
		auto first = x509->pCertInfo->SerialNumber.pbData;
		auto last = first + x509->pCertInfo->SerialNumber.cbData;

		while (last != first && !last[-1])
		{
			--last;
		}

		return
		{
			serial_number_buf.begin(),
			std::reverse_copy(first, last, serial_number_buf.begin())
		};
	}

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

std::span<const std::byte> certificate::as_bytes () const noexcept
{
	return
	{
		reinterpret_cast<const std::byte *>(impl_->x509->pbCertEncoded),
		impl_->x509->cbCertEncoded
	};
}

int certificate::version () const noexcept
{
	return impl_->x509->pCertInfo->dwVersion + 1;
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

namespace {

template <typename T, size_t Size>
struct asn_decoder
{
	LPCSTR type;
	std::span<const BYTE> asn;
	const DWORD buffer_size;
	temporary_buffer<Size> buffer;
	const T &value;

	asn_decoder (LPCSTR type, std::span<const BYTE> asn) noexcept
		: type{type}
		, asn{asn}
		, buffer_size{decode(nullptr, 0)}
		, buffer{std::nothrow, buffer_size}
		, value{*reinterpret_cast<const T *>(buffer.get())}
	{
		decode(buffer.get(), buffer_size);
	}

	DWORD decode (void *buf, DWORD buf_size) const noexcept
	{
		static constexpr DWORD decode_flags =
			CRYPT_DECODE_NOCOPY_FLAG |
			CRYPT_DECODE_SHARE_OID_STRING_FLAG
		;

		DWORD size = buf_size;
		::CryptDecodeObjectEx(
			X509_ASN_ENCODING,
			type,
			asn.data(),
			static_cast<DWORD>(asn.size()),
			decode_flags,
			nullptr,
			buf,
			&size
		);
		return size;
	}
};

} // namespace

struct distinguished_name::impl_type
{
	certificate::impl_ptr owner;
	asn_decoder<CERT_NAME_INFO, 3 * 1024> name;
	size_t entries;

	impl_type (certificate::impl_ptr owner, const CERT_NAME_BLOB &name) noexcept
		: owner{owner}
		, name{X509_NAME, {name.pbData, name.cbData}}
		, entries{this->name.value.cRDN}
	{ }

	static result<distinguished_name> make (certificate::impl_ptr owner, const CERT_NAME_BLOB &name) noexcept
	{
		impl_ptr list{new(std::nothrow) impl_type{owner, name}};
		if (!list || !list->name.buffer.get())
		{
			return make_unexpected(std::errc::not_enough_memory);
		}
		return distinguished_name{list};
	}
};

namespace {

template <size_t N>
void copy_oid (const CERT_RDN_ATTR &entry, char (&buf)[N]) noexcept
{
	std::strncpy(buf, entry.pszObjId, N - 1);
	buf[N - 1] = '\0';
}

template <size_t N>
void copy_value (const CERT_RDN_ATTR &entry, char (&buf)[N]) noexcept
{
	::CertRDNValueToStr(
		entry.dwValueType,
		const_cast<CERT_RDN_VALUE_BLOB *>(&entry.Value),
		buf,
		N
	);
}

} // namespace

void distinguished_name::const_iterator::load_entry_at (size_t index) noexcept
{
	if (index < owner_->entries)
	{
		auto &entry = owner_->name.value.rgRDN[index].rgRDNAttr[0];
		copy_oid(entry, entry_.oid);
		copy_value(entry, entry_.value);
		return;
	}
	owner_ = nullptr;
}

result<distinguished_name> certificate::issuer_name () const noexcept
{
	return distinguished_name::impl_type::make(impl_, impl_->x509->pCertInfo->Issuer);
}

result<distinguished_name> certificate::subject_name () const noexcept
{
	return distinguished_name::impl_type::make(impl_, impl_->x509->pCertInfo->Subject);
}

} // namespace pal::crypto
