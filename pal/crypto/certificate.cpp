#include <pal/crypto/certificate>
#include <pal/scoped_alloc>
#include <cctype>
#include <memory>

#if __pal_os_linux //{{{1

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
	return d2i_X509(nullptr, &der_ptr, der.size());
}


bool certificate::operator== (const certificate &that) const noexcept
{
	if (impl_.ref && that.impl_.ref)
	{
		return ::X509_cmp(impl_.ref, that.impl_.ref) == 0;
	}
	return !impl_.ref && !that.impl_.ref;
}


int certificate::version () const noexcept
{
	return impl_.ref ? X509_get_version(impl_.ref) + 1 : 0;
}


namespace {


certificate::time_type to_time (const ::ASN1_TIME *time) noexcept
{
	if (::ASN1_TIME_check(const_cast<::ASN1_TIME *>(time)) == 0)
	{
		return {};
	}

	std::tm tm{};
	auto in = static_cast<const unsigned char *>(time->data);

	if (time->type == V_ASN1_UTCTIME)
	{
		// two-digit year
		tm.tm_year = (in[0] - '0') * 10 + (in[1] - '0');
		in += 2;

		if (tm.tm_year < 70)
		{
			tm.tm_year += 100;
		}
	}
	else if (time->type == V_ASN1_GENERALIZEDTIME)
	{
		// four-digit year
		tm.tm_year =
			(in[0] - '0') * 1000 +
			(in[1] - '0') * 100 +
			(in[2] - '0') * 10 +
			(in[3] - '0');
		in += 4;

		tm.tm_year -= 1900;
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

	return certificate::clock_type::from_time_t(mktime(&tm));
}


} // namespace


certificate::time_type certificate::not_before () const noexcept
{
	return impl_ ?
		to_time(X509_get_notBefore(impl_.ref)) :
		certificate::time_type{}
	;
}


certificate::time_type certificate::not_after () const noexcept
{
	return impl_ ?
		to_time(X509_get_notAfter(impl_.ref)) :
		certificate::time_type{}
	;
}


void certificate::digest (std::byte *result, hash_fn h) const noexcept
{
	size_t size = i2d_X509(impl_.ref, nullptr);
	if (auto der = scoped_alloc<std::byte, 8192>(std::nothrow, size))
	{
		auto ptr = reinterpret_cast<uint8_t *>(der.get());
		i2d_X509(impl_.ref, &ptr);
		h(result, {der.get(), size});
	}
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


unique_ref<::CFArrayRef> copy_values (
	::SecCertificateRef cert,
	::CFTypeRef oid,
	std::error_code &error) noexcept
{
	if (cert)
	{
		unique_ref<::CFArrayRef> keys = ::CFArrayCreate(
			nullptr,
			&oid, 1,
			&kCFTypeArrayCallBacks
		);
		unique_ref<::CFDictionaryRef> dir = ::SecCertificateCopyValues(
			cert,
			keys.ref,
			nullptr
		);

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

		error.clear();
	}
	else
	{
		error = std::make_error_code(std::errc::bad_address);
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


int certificate::version () const noexcept
{
	std::error_code ignore_error;
	if (auto value = copy_values(impl_.ref, ::kSecOIDX509V1Version, ignore_error))
	{
		char buf[16];
		return std::atoi(c_str(value.ref, buf));
	}
	return 0;
}


namespace {

certificate::time_type to_time (::SecCertificateRef cert, ::CFTypeRef oid) noexcept
{
	std::error_code ignore_error;
	if (auto value = copy_values(cert, oid, ignore_error))
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


certificate::time_type certificate::not_before () const noexcept
{
	return to_time(impl_.ref, kSecOIDX509V1ValidityNotBefore);
}


certificate::time_type certificate::not_after () const noexcept
{
	return to_time(impl_.ref, kSecOIDX509V1ValidityNotAfter);
}


void certificate::digest (std::byte *result, hash_fn h) const noexcept
{
	unique_ref<::CFDataRef> der = ::SecCertificateCopyData(impl_.ref);
	h(result, as_bytes(der.ref));
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


int certificate::version () const noexcept
{
	return impl_.ref ? impl_.ref->pCertInfo->dwVersion + 1 : 0;
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


certificate::time_type certificate::not_before () const noexcept
{
	return impl_ ?
		to_time(impl_.ref->pCertInfo->NotBefore) :
		certificate::time_type{}
	;
}


certificate::time_type certificate::not_after () const noexcept
{
	return impl_ ?
		to_time(impl_.ref->pCertInfo->NotAfter) :
		certificate::time_type{}
	;
}


void certificate::digest (std::byte *result, hash_fn h) const noexcept
{
	std::span der{impl_.ref->pbCertEncoded, impl_.ref->cbCertEncoded};
	h(result, std::as_bytes(der));
}


#endif //}}}1


} // namespace crypto


__pal_end
