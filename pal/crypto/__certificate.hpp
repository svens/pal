#pragma once

/**
 * \file pal/crypto/__certificate.hpp
 * Platform certificate implementation types (internal)
 */

#include <pal/crypto/__crypto.hpp>
#include <pal/crypto/certificate.hpp>
#include <memory>

#if __pal_crypto_openssl
	#include <openssl/x509.h>
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

#elif __pal_crypto_windows //{{{1

struct cert_deleter
{
	void operator() (const ::CERT_CONTEXT *p) const noexcept
	{
		::CertFreeCertificateContext(p);
	}
};
using cert_ptr = std::unique_ptr<const ::CERT_CONTEXT, cert_deleter>;

#endif //}}}1

struct certificate::impl_type
{
	cert_ptr x509;

	explicit impl_type (cert_ptr x509) noexcept
		: x509{std::move(x509)}
	{
	}

	impl_type (const impl_type &) = delete;
	impl_type &operator= (const impl_type &) = delete;

	static certificate to_api (certificate::impl_ptr impl) noexcept;
};

inline certificate certificate::impl_type::to_api (certificate::impl_ptr impl) noexcept
{
	return certificate{std::move(impl)};
}

} // namespace pal::crypto
