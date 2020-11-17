#include <pal/crypto/certificate>

#if __pal_os_linux //{{{1
#elif __pal_os_macos //{{{1
#elif __pal_os_windows //{{{1
#endif //}}}1


__pal_begin


namespace crypto {


#if __pal_os_linux //{{{1


bool certificate::operator== (const certificate &that) const noexcept
{
	if (impl_.ref && that.impl_.ref)
	{
		return ::X509_cmp(impl_.ref, that.impl_.ref) == 0;
	}
	return !impl_.ref && !that.impl_.ref;
}


#elif __pal_os_macos //{{{1


bool certificate::operator== (const certificate &that) const noexcept
{
	if (impl_.ref && that.impl_.ref)
	{
		return ::CFEqual(impl_.ref, that.impl_.ref);
	}
	return !impl_.ref && !that.impl_.ref;
}


#elif __pal_os_windows //{{{1


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


#endif //}}}1


} // namespace crypto


__pal_end
