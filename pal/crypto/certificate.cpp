#include <pal/crypto/certificate>
#include <pal/scoped_alloc>
#include <cctype>
#include <memory>

#if __pal_os_linux //{{{1
	#include <openssl/asn1.h>
	#include <openssl/x509v3.h>
#elif __pal_os_macos //{{{1
	#include <Security/SecCertificateOIDs.h>
#elif __pal_os_windows //{{{1

#endif //}}}1


__pal_begin


namespace crypto {


namespace {


// can't use pal::from_base4 because of whitespaces
size_t base64_decode (const std::string_view &base64, std::byte *decoded) noexcept
{
	static constexpr unsigned char lookup[] =
		// _0  _1  _2  _3  _4  _5  _6  _7  _8  _9  _a  _b  _c  _d  _e  _f
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff" // 0_
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff" // 1_
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\x3e\xff\xff\xff\x3f" // 2_
		"\x34\x35\x36\x37\x38\x39\x3a\x3b\x3c\x3d\xff\xff\xff\xff\xff\xff" // 3_
		"\xff\x00\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e" // 4_
		"\x0f\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\xff\xff\xff\xff\xff" // 5_
		"\xff\x1a\x1b\x1c\x1d\x1e\x1f\x20\x21\x22\x23\x24\x25\x26\x27\x28" // 6_
		"\x29\x2a\x2b\x2c\x2d\x2e\x2f\x30\x31\x32\x33\xff\xff\xff\xff\xff" // 7_
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff" // 8_
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff" // 9_
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff" // a_
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff" // b_
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff" // c_
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff" // d_
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff" // e_
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff" // f_
		;

	auto p = decoded;
	int32_t val = 0, valb = -8;
	for (uint8_t ch: base64)
	{
		if (lookup[ch] != 0xff)
		{
			val = (val << 6) | lookup[ch];
			if ((valb += 6) >= 0)
			{
				*p++ = static_cast<std::byte>((val >> valb) & 0xff);
				valb -= 8;
			}
		}
		else if (std::isspace(ch))
		{
			continue;
		}
		else if (ch == '=')
		{
			break;
		}
		else
		{
			return 0;
		}
	}
	return p - decoded;
}


size_t pem_to_der (std::string_view pem, std::byte *der) noexcept
{
	static constexpr std::string_view
		prefix = "-----BEGIN CERTIFICATE-----\n",
		suffix = "\n-----END CERTIFICATE-----\n";

	if (pem.starts_with(prefix))
	{
		pem.remove_prefix(prefix.size());
	}
	else
	{
		return 0;
	}

	auto suffix_pos = pem.find(suffix);
	if (suffix_pos != pem.npos)
	{
		pem.remove_suffix(pem.size() - suffix_pos);
	}
	else
	{
		return 0;
	}

	return base64_decode(pem, der);
}


} // namespace


certificate certificate::from_pem (
	const std::string_view &pem,
	std::error_code &error) noexcept
{
	if (auto der = scoped_alloc<std::byte, 8192>(std::nothrow, pem.size() / 4 * 3))
	{
		if (auto der_size = pem_to_der(pem, der.get()))
		{
			if (auto cert = load_der({der.get(), der_size}))
			{
				return cert;
			}
		}
		error = std::make_error_code(std::errc::invalid_argument);
	}
	else
	{
		error = std::make_error_code(std::errc::not_enough_memory);
	}
	return {};
}


#if __pal_os_linux //{{{1


__bits::x509 certificate::load_der (const std::span<const std::byte> &der) noexcept
{
	auto der_ptr = reinterpret_cast<const uint8_t *>(der.data());
	return ::d2i_X509(nullptr, &der_ptr, der.size());
}


bool certificate::operator== (const certificate &that) const noexcept
{
	if (impl_.ref && that.impl_.ref)
	{
		return ::X509_cmp(impl_.ref, that.impl_.ref) == 0;
	}
	return !impl_.ref && !that.impl_.ref;
}


int certificate::version (const __bits::x509 &x509) noexcept
{
	return ::X509_get_version(x509.ref) + 1;
}


namespace {


certificate::time_type to_time (const ::ASN1_TIME *time) noexcept
{
	std::tm tm{};

	#if OPENSSL_VERSION_NUMBER >= 0x10101000
		::ASN1_TIME_to_tm(time, &tm);
	#else
		if (::ASN1_TIME_check(time))
		{
			auto in = time->data;

			if (time->type == V_ASN1_GENERALIZEDTIME)
			{
				tm.tm_year =
					(in[0] - '0') * 1000 +
					(in[1] - '0') * 100 +
					(in[2] - '0') * 10 +
					(in[3] - '0');
				in += 4;

				tm.tm_year -= 1900;
			}
			else if (time->type == V_ASN1_UTCTIME)
			{
				tm.tm_year = (in[0] - '0') * 10 + (in[1] - '0');
				in += 2;

				if (tm.tm_year < 70)
				{
					tm.tm_year += 100;
				}
			}

			tm.tm_mon = (in[0] - '0') * 10 + (in[1] - '0') - 1;
			in += 2;

			tm.tm_mday = (in[0] - '0') * 10 + (in[1] - '0');
			in += 2;

			tm.tm_hour = (in[0] - '0') * 10 + (in[1] - '0');
			in += 2;

			tm.tm_min = (in[0] - '0') * 10 + (in[1] - '0');
			in += 2;

			tm.tm_sec = (in[0] - '0') * 10 + (in[1] - '0');
		}
	#endif

	return certificate::clock_type::from_time_t(mktime(&tm));
}


} // namespace


certificate::time_type certificate::not_before (const __bits::x509 &x509) noexcept
{
	return to_time(::X509_get_notBefore(x509.ref));
}


certificate::time_type certificate::not_after (const __bits::x509 &x509) noexcept
{
	return to_time(::X509_get_notAfter(x509.ref));
}


void certificate::digest (std::byte *result, hash_fn h) const noexcept
{
	size_t size = ::i2d_X509(impl_.ref, nullptr);
	if (auto der = scoped_alloc<std::byte, 8192>(std::nothrow, size))
	{
		auto ptr = reinterpret_cast<uint8_t *>(der.get());
		::i2d_X509(impl_.ref, &ptr);
		h(result, {der.get(), size});
	}
}


std::span<uint8_t> certificate::serial_number (
	const __bits::x509 &x509,
	std::span<uint8_t> dest) noexcept
{
	auto sn = ::X509_get_serialNumber(x509.ref);
	size_t required_size = sn->length;
	if (required_size > dest.size())
	{
		return {static_cast<uint8_t *>(0), required_size};
	}

	std::unique_ptr<::BIGNUM, void(*)(::BIGNUM *)> bn
	{
		::ASN1_INTEGER_to_BN(sn, nullptr),
		&::BN_free
	};
	if (bn)
	{
		dest = dest.first(BN_num_bytes(bn.get()));
		::BN_bn2bin(bn.get(), dest.data());
		return dest;
	}

	return {static_cast<uint8_t *>(0), required_size};
}


std::span<uint8_t> certificate::authority_key_identifier (
	const __bits::x509 &x509,
	std::span<uint8_t> dest) noexcept
{
	auto index = ::X509_get_ext_by_NID(x509.ref, NID_authority_key_identifier, -1);
	if (index < 0)
	{
		return dest.first(0);
	}

	unique_ref<AUTHORITY_KEYID *, &::AUTHORITY_KEYID_free> decoded
	{
		static_cast<AUTHORITY_KEYID *>(
			::X509V3_EXT_d2i(::X509_get_ext(x509.ref, index))
		)
	};

	if (!decoded)
	{
		return std::span<uint8_t>{};
	}

	size_t required_size = decoded.ref->keyid->length;
	if (required_size > dest.size())
	{
		return {static_cast<uint8_t *>(0), required_size};
	}

	dest = dest.first(required_size);
	auto key_id = decoded.ref->keyid->data;
	for (auto &b: dest)
	{
		b = *key_id++;
	}
	return dest;
}


std::span<uint8_t> certificate::subject_key_identifier (
	const __bits::x509 &x509,
	std::span<uint8_t> dest) noexcept
{
	auto index = ::X509_get_ext_by_NID(x509.ref, NID_subject_key_identifier, -1);
	if (index < 0)
	{
		return dest.first(0);
	}

	unique_ref<ASN1_OCTET_STRING *, &::ASN1_OCTET_STRING_free> decoded
	{
		static_cast<ASN1_OCTET_STRING *>(
			::X509V3_EXT_d2i(::X509_get_ext(x509.ref, index))
		)
	};

	if (!decoded)
	{
		return std::span<uint8_t>{};
	}

	size_t required_size = decoded.ref->length;
	if (required_size > dest.size())
	{
		return {static_cast<uint8_t *>(0), required_size};
	}

	dest = dest.first(required_size);
	auto key_id = decoded.ref->data;
	for (auto &b: dest)
	{
		b = *key_id++;
	}
	return dest;
}


bool certificate::issued_by (const __bits::x509 &a, const __bits::x509 &b) noexcept
{
	return ::X509_check_issued(b.ref, a.ref) == X509_V_OK;
}


namespace {


inline const char *c_str (const ::ASN1_STRING *s) noexcept
{
	return reinterpret_cast<const char *>(
		#if OPENSSL_VERSION_NUMBER < 0x10100000
			::ASN1_STRING_data(const_cast<::ASN1_STRING *>(s))
		#else
			::ASN1_STRING_get0_data(s)
		#endif
	);
}


certificate::name_type make_name (X509_NAME *name)
{
	char buf[1024];
	certificate::name_type result;
	for (auto i = 0;  i != ::X509_NAME_entry_count(name);  ++i)
	{
		auto entry = ::X509_NAME_get_entry(name, i);
		::OBJ_obj2txt(buf, sizeof(buf), ::X509_NAME_ENTRY_get_object(entry), 1);
		result.emplace_back(buf, c_str(::X509_NAME_ENTRY_get_data(entry)));
	}
	return result;
}


certificate::name_type make_name (X509_NAME *name, const std::string_view &oid)
{
	certificate::name_type result;

	std::string oid_str{oid};
	const auto filter = ::OBJ_txt2nid(oid_str.c_str());
	if (filter == NID_undef)
	{
		return result;
	}

	for (auto i = 0;  i != ::X509_NAME_entry_count(name);  ++i)
	{
		auto entry = ::X509_NAME_get_entry(name, i);
		if (::OBJ_obj2nid(X509_NAME_ENTRY_get_object(entry)) == filter)
		{
			result.emplace_back(oid_str, c_str(::X509_NAME_ENTRY_get_data(entry)));
		}
	}
	return result;
}


} // namespace


certificate::name_type certificate::issuer (const __bits::x509 &x509)
{
	return make_name(X509_get_issuer_name(x509.ref));
}


certificate::name_type certificate::issuer (const __bits::x509 &x509, const std::string_view &oid)
{
	return make_name(X509_get_issuer_name(x509.ref), oid);
}


certificate::name_type certificate::subject (const __bits::x509 &x509)
{
	return make_name(X509_get_subject_name(x509.ref));
}


certificate::name_type certificate::subject (const __bits::x509 &x509, const std::string_view &oid)
{
	return make_name(X509_get_subject_name(x509.ref), oid);
}


#elif __pal_os_macos //{{{1


namespace {


inline unique_ref<::CFDataRef> to_data_ref (const std::span<const std::byte> &span) noexcept
{
	return ::CFDataCreateWithBytesNoCopy(
		nullptr,
		reinterpret_cast<const UInt8 *>(span.data()),
		span.size(),
		kCFAllocatorNull
	);
}


inline std::span<const std::byte> as_bytes (::CFDataRef data) noexcept
{
	return std::span<const std::byte>
	{
		reinterpret_cast<const std::byte *>(::CFDataGetBytePtr(data)),
		static_cast<size_t>(::CFDataGetLength(data))
	};
}


template <size_t N>
inline const char *c_str (::CFTypeRef s, char (&buf)[N]) noexcept
{
	constexpr auto encoding = ::kCFStringEncodingUTF8;

	auto cfstr = static_cast<::CFStringRef>(s);
	if (auto p = ::CFStringGetCStringPtr(cfstr, encoding))
	{
		return p;
	}

	::CFStringGetCString(cfstr, buf, N, encoding);
	return static_cast<const char *>(&buf[0]);
}


unique_ref<::CFArrayRef> copy_values (::SecCertificateRef cert, ::CFTypeRef oid) noexcept
{
	unique_ref<::CFArrayRef> keys = ::CFArrayCreate(nullptr, &oid, 1, &kCFTypeArrayCallBacks);
	unique_ref<::CFDictionaryRef> dir = ::SecCertificateCopyValues(cert, keys.ref, nullptr);

	::CFTypeRef values;
	if (::CFDictionaryGetValueIfPresent(dir.ref, oid, &values))
	{
		if (::CFDictionaryGetValueIfPresent(
				static_cast<::CFDictionaryRef>(values),
				::kSecPropertyKeyValue,
				&values))
		{
			return static_cast<::CFArrayRef>(::CFRetain(values));
		}
	}
	return {};
}


} // namespace


__bits::x509 certificate::load_der (const std::span<const std::byte> &der) noexcept
{
	return ::SecCertificateCreateWithData(nullptr, to_data_ref(der).ref);
}


bool certificate::operator== (const certificate &that) const noexcept
{
	if (impl_.ref && that.impl_.ref)
	{
		return ::CFEqual(impl_.ref, that.impl_.ref);
	}
	return !impl_.ref && !that.impl_.ref;
}


int certificate::version (const __bits::x509 &x509) noexcept
{
	if (auto value = copy_values(x509.ref, ::kSecOIDX509V1Version))
	{
		char buf[16];
		return std::atoi(c_str(value.ref, buf));
	}
	return 0;
}


namespace {

certificate::time_type to_time (::SecCertificateRef cert, ::CFTypeRef oid) noexcept
{
	if (auto value = copy_values(cert, oid))
	{
		::CFAbsoluteTime time;
		::CFNumberGetValue(
			(::CFNumberRef)value.ref,
			::kCFNumberDoubleType,
			&time
		);
		return certificate::clock_type::from_time_t(
			time + ::kCFAbsoluteTimeIntervalSince1970
		);
	}
	return {};
}

} // namespace


certificate::time_type certificate::not_before (const __bits::x509 &x509) noexcept
{
	return to_time(x509.ref, kSecOIDX509V1ValidityNotBefore);
}


certificate::time_type certificate::not_after (const __bits::x509 &x509) noexcept
{
	return to_time(x509.ref, kSecOIDX509V1ValidityNotAfter);
}


void certificate::digest (std::byte *result, hash_fn h) const noexcept
{
	unique_ref<::CFDataRef> der = ::SecCertificateCopyData(impl_.ref);
	h(result, as_bytes(der.ref));
}


std::span<uint8_t> certificate::serial_number (
	const __bits::x509 &x509,
	std::span<uint8_t> dest) noexcept
{
	unique_ref<::CFDataRef> value = ::SecCertificateCopySerialNumberData(x509.ref, nullptr);

	auto first = ::CFDataGetBytePtr(value.ref);
	auto last = first + ::CFDataGetLength(value.ref);
	while (first != last && !*first)
	{
		++first;
	}

	size_t required_size = last - first;
	if (required_size > dest.size())
	{
		return {static_cast<uint8_t *>(0), required_size};
	}

	dest = dest.first(required_size);
	for (auto &b: dest)
	{
		b = *first++;
	}
	return dest;
}


namespace {


std::span<uint8_t> key_id (
	::SecCertificateRef cert,
	::CFTypeRef oid,
	std::span<uint8_t> dest) noexcept
{
	if (auto values = copy_values(cert, oid))
	{
		auto value = (::CFDataRef)::CFDictionaryGetValue(
			(::CFDictionaryRef)::CFArrayGetValueAtIndex(values.ref, 1),
			::kSecPropertyKeyValue
		);

		auto first = ::CFDataGetBytePtr(value);
		auto last = first + ::CFDataGetLength(value);
		size_t required_size = last - first;
		if (required_size > dest.size())
		{
			return {static_cast<uint8_t *>(0), required_size};
		}

		dest = dest.first(required_size);
		for (auto &b: dest)
		{
			b = *first++;
		}
		return dest;
	}
	return dest.first(0);
}


} // namespace


std::span<uint8_t> certificate::authority_key_identifier (
	const __bits::x509 &x509,
	std::span<uint8_t> dest) noexcept
{
	return key_id(x509.ref, kSecOIDAuthorityKeyIdentifier, dest);
}


std::span<uint8_t> certificate::subject_key_identifier (
	const __bits::x509 &x509,
	std::span<uint8_t> dest) noexcept
{
	return key_id(x509.ref, kSecOIDSubjectKeyIdentifier, dest);
}


bool certificate::issued_by (const __bits::x509 &a, const __bits::x509 &b) noexcept
{
	unique_ref<::CFDataRef>
		a_issuer = ::SecCertificateCopyNormalizedIssuerSequence(a.ref),
		b_subject = ::SecCertificateCopyNormalizedSubjectSequence(b.ref);

	auto size_1 = ::CFDataGetLength(b_subject.ref);
	auto size_2 = ::CFDataGetLength(a_issuer.ref);
	if (size_1 != size_2)
	{
		return false;
	}

	auto data_1 = ::CFDataGetBytePtr(b_subject.ref);
	auto data_2 = ::CFDataGetBytePtr(a_issuer.ref);
	return std::equal(data_1, data_1 + size_1, data_2);
}


namespace {


certificate::name_type make_name (::SecCertificateRef cert, ::CFTypeRef id)
{
	certificate::name_type result;
	if (auto values = copy_values(cert, id))
	{
		char key_buf[1024], value_buf[1024];
		for (auto i = 0;  i != ::CFArrayGetCount(values.ref);  ++i)
		{
			auto key = (CFDictionaryRef)::CFArrayGetValueAtIndex(values.ref, i);
			result.emplace_back(
				c_str(::CFDictionaryGetValue(key, kSecPropertyKeyLabel), key_buf),
				c_str(::CFDictionaryGetValue(key, kSecPropertyKeyValue), value_buf)
			);
		}
	}
	return result;
}


certificate::name_type make_name (::SecCertificateRef cert, ::CFTypeRef id, const std::string_view &oid)
{
	certificate::name_type result;
	if (auto values = copy_values(cert, id))
	{
		unique_ref<::CFStringRef> filter = ::CFStringCreateWithBytesNoCopy(
			nullptr,
			reinterpret_cast<const uint8_t *>(oid.data()),
			oid.size(),
			kCFStringEncodingUTF8,
			false,
			kCFAllocatorNull
		);

		char buf[1024];
		for (auto i = 0;  i != ::CFArrayGetCount(values.ref);  ++i)
		{
			auto key = (CFDictionaryRef)::CFArrayGetValueAtIndex(values.ref, i);
			if (::CFEqual(::CFDictionaryGetValue(key, kSecPropertyKeyLabel), filter.ref))
			{
				result.emplace_back(
					oid,
					c_str(::CFDictionaryGetValue(key, kSecPropertyKeyValue), buf)
				);
			}
		}
	}
	return result;
}


} // namespace


certificate::name_type certificate::issuer (const __bits::x509 &x509)
{
	return make_name(x509.ref, ::kSecOIDX509V1IssuerName);
}


certificate::name_type certificate::issuer (const __bits::x509 &x509, const std::string_view &oid)
{
	return make_name(x509.ref, ::kSecOIDX509V1IssuerName, oid);
}


certificate::name_type certificate::subject (const __bits::x509 &x509)
{
	return make_name(x509.ref, ::kSecOIDX509V1SubjectName);
}


certificate::name_type certificate::subject (const __bits::x509 &x509, const std::string_view &oid)
{
	return make_name(x509.ref, ::kSecOIDX509V1SubjectName, oid);
}


#elif __pal_os_windows //{{{1


__bits::x509 certificate::load_der (const std::span<const std::byte> &der) noexcept
{
	return ::CertCreateCertificateContext(
		X509_ASN_ENCODING,
		reinterpret_cast<const BYTE *>(der.data()),
		static_cast<DWORD>(der.size())
	);
}


bool certificate::operator== (const certificate &that) const noexcept
{
	if (impl_.ref && that.impl_.ref)
	{
		return ::CertCompareCertificate(
			X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
			impl_.ref->pCertInfo,
			that.impl_.ref->pCertInfo
		) != 0;
	}
	return !impl_.ref && !that.impl_.ref;
}


int certificate::version (const __bits::x509 &x509) noexcept
{
	return x509.ref->pCertInfo->dwVersion + 1;
}


namespace {

certificate::time_type to_time (const FILETIME &time) noexcept
{
	SYSTEMTIME sys_time;
	if (::FileTimeToSystemTime(&time, &sys_time))
	{
		std::tm tm{};
		tm.tm_sec = sys_time.wSecond;
		tm.tm_min = sys_time.wMinute;
		tm.tm_hour = sys_time.wHour;
		tm.tm_mday = sys_time.wDay;
		tm.tm_mon = sys_time.wMonth - 1;
		tm.tm_year = sys_time.wYear - 1900;
		return certificate::clock_type::from_time_t(mktime(&tm));
	}
	return {};
}

} // namespace


certificate::time_type certificate::not_before (const __bits::x509 &x509) noexcept
{
	return to_time(x509.ref->pCertInfo->NotBefore);
}


certificate::time_type certificate::not_after (const __bits::x509 &x509) noexcept
{
	return to_time(x509.ref->pCertInfo->NotAfter);
}


void certificate::digest (std::byte *result, hash_fn h) const noexcept
{
	std::span der{impl_.ref->pbCertEncoded, impl_.ref->cbCertEncoded};
	h(result, std::as_bytes(der));
}


std::span<uint8_t> certificate::serial_number (
	const __bits::x509 &x509,
	std::span<uint8_t> dest) noexcept
{
	auto first = x509.ref->pCertInfo->SerialNumber.pbData;
	auto last = first + x509.ref->pCertInfo->SerialNumber.cbData;

	while (last != first && !last[-1])
	{
		--last;
	}

	size_t required_size = last - first;
	if (required_size > dest.size())
	{
		return {static_cast<uint8_t *>(0), required_size};
	}

	dest = dest.first(required_size);
	for (auto &b: dest)
	{
		b = *--last;
	}
	return dest;
}


std::span<uint8_t> certificate::authority_key_identifier (
	const __bits::x509 &x509,
	std::span<uint8_t> dest) noexcept
{
	auto ext = ::CertFindExtension(szOID_AUTHORITY_KEY_IDENTIFIER2,
		x509.ref->pCertInfo->cExtension,
		x509.ref->pCertInfo->rgExtension
	);
	if (!ext)
	{
		return dest.first(0);
	}

	unique_ref<CERT_AUTHORITY_KEY_ID2_INFO *, [](CERT_AUTHORITY_KEY_ID2_INFO *p){ ::LocalFree(p); }> decoded;
	DWORD length = 0;
	::CryptDecodeObjectEx(X509_ASN_ENCODING,
		X509_AUTHORITY_KEY_ID2,
		ext->Value.pbData,
		ext->Value.cbData,
		CRYPT_DECODE_ALLOC_FLAG,
		0,
		&decoded.ref,
		&length
	);
	if (!decoded)
	{
		return std::span<uint8_t>{};
	}

	size_t required_size = decoded.ref->KeyId.cbData;
	if (required_size > dest.size())
	{
		return {static_cast<uint8_t *>(0), required_size};
	}

	dest = dest.first(required_size);
	auto key_id = decoded.ref->KeyId.pbData;
	for (auto &b: dest)
	{
		b = *key_id++;
	}
	return dest;
}


std::span<uint8_t> certificate::subject_key_identifier (
	const __bits::x509 &x509,
	std::span<uint8_t> dest) noexcept
{
	auto ext = ::CertFindExtension(szOID_SUBJECT_KEY_IDENTIFIER,
		x509.ref->pCertInfo->cExtension,
		x509.ref->pCertInfo->rgExtension
	);
	if (!ext)
	{
		return dest.first(0);
	}

	unique_ref<CRYPT_DATA_BLOB *, [](CRYPT_DATA_BLOB *p){ ::LocalFree(p); }> decoded;
	DWORD length = 0;
	::CryptDecodeObjectEx(X509_ASN_ENCODING,
		szOID_SUBJECT_KEY_IDENTIFIER,
		ext->Value.pbData,
		ext->Value.cbData,
		CRYPT_DECODE_ALLOC_FLAG,
		0,
		&decoded.ref,
		&length
	);
	if (!decoded)
	{
		return std::span<uint8_t>{};
	}

	size_t required_size = decoded.ref->cbData;
	if (required_size > dest.size())
	{
		return {static_cast<uint8_t *>(0), required_size};
	}

	dest = dest.first(required_size);
	auto key_id = decoded.ref->pbData;
	for (auto &b: dest)
	{
		b = *key_id++;
	}
	return dest;
}


bool certificate::issued_by (const __bits::x509 &a, const __bits::x509 &b) noexcept
{
	return ::CertCompareCertificateName(
		X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
		&b.ref->pCertInfo->Subject,
		&a.ref->pCertInfo->Issuer
	);
}


namespace {


using cert_name_info = unique_ref<
	CERT_NAME_INFO *,
	[](CERT_NAME_INFO *p)
	{
		::LocalFree(p);
	}
>;


inline cert_name_info decode_name (const CERT_NAME_BLOB &name)
{
	constexpr auto decode_flags =
		CRYPT_DECODE_ALLOC_FLAG |
		CRYPT_DECODE_NOCOPY_FLAG |
		CRYPT_DECODE_SHARE_OID_STRING_FLAG;

	cert_name_info rdn;
	DWORD size{};
	::CryptDecodeObjectEx(
		X509_ASN_ENCODING,
		X509_NAME,
		name.pbData,
		name.cbData,
		decode_flags,
		nullptr,
		&rdn.ref,
		&size
	);
	return rdn;
}


certificate::name_type make_name (const CERT_NAME_BLOB &blob, const std::string_view &oid)
{
	certificate::name_type result;
	if (auto rdn = decode_name(blob))
	{
		for (size_t i = 0;  i != rdn.ref->cRDN;  ++i)
		{
			for (size_t j = 0;  j != rdn.ref->rgRDN[i].cRDNAttr;  ++j)
			{
				auto &attr = rdn.ref->rgRDN[i].rgRDNAttr[j];
				if (oid.empty() || oid == attr.pszObjId)
				{
					char buf[1024];
					::CertRDNValueToStr(
						attr.dwValueType,
						&attr.Value,
						buf,
						sizeof(buf)
					);
					result.emplace_back(attr.pszObjId, buf);
				}
			}
		}
	}
	return result;
}


} // namespace


certificate::name_type certificate::issuer (const __bits::x509 &x509)
{
	return make_name(x509.ref->pCertInfo->Issuer, {});
}


certificate::name_type certificate::issuer (const __bits::x509 &x509, const std::string_view &oid)
{
	return make_name(x509.ref->pCertInfo->Issuer, oid);
}


certificate::name_type certificate::subject (const __bits::x509 &x509)
{
	return make_name(x509.ref->pCertInfo->Subject, {});
}


certificate::name_type certificate::subject (const __bits::x509 &x509, const std::string_view &oid)
{
	return make_name(x509.ref->pCertInfo->Subject, oid);
}


#endif //}}}1


} // namespace crypto


__pal_end
