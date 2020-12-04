#include <pal/crypto/__bits/x509>

#if __pal_os_linux
	#include <openssl/bio.h>
	#include <openssl/pem.h>
	#include <sys/types.h>
	#include <sys/stat.h>
	#include <unistd.h>
#elif __pal_os_macos
	#include <Security/SecItem.h>
#endif


__pal_begin


namespace crypto::__bits {


#if __pal_os_linux //{{{1


namespace {


using bio_ptr = unique_ref<BIO *, &::BIO_free_all>;


const char *ca_filename () noexcept
{
	// see https://www.happyassassin.net/2015/01/12/a-note-about-ssltls-trusted-certificate-stores-and-platforms/
	static const char *files[] =
	{
		std::getenv(::X509_get_default_cert_file_env()),
		::X509_get_default_cert_file(),
		"/etc/pki/tls/certs/ca-bundle.crt",
		"/etc/ssl/certs/ca-certificates.crt",
		nullptr
	};
	auto file = files[0];
	for (/**/;  file;  file++)
	{
		struct stat st;
		if (stat(file, &st) == 0 && S_ISREG(st.st_mode))
		{
			break;
		}
	}
	return file;
}


bio_ptr ca_file ()
{
	static auto filename = ca_filename();
	bio_ptr bio{::BIO_new(::BIO_s_file())};
	BIO_read_filename(bio.ref, filename);
	return bio;
}


} // namespace


x509_store_loop::x509_store_loop () noexcept
	: store(PEM_X509_INFO_read_bio(ca_file().ref, nullptr, nullptr, nullptr))
	, count(store ? sk_X509_INFO_num(store.ref) : 0)
	, index(-1)
{ }


x509 x509_store_loop::next () noexcept
{
	if (++index < count)
	{
		return x509{inc_ref(sk_X509_INFO_value(store.ref, index)->x509)};
	}
	return {};
}


#elif __pal_os_macos //{{{1


namespace {


inline auto keychain_query () noexcept
{
	static const void
		*keys[] =
		{
			::kSecClass,
			::kSecMatchTrustedOnly,
			::kSecMatchLimit,
		},
		*values[] =
		{
			::kSecClassCertificate,
			::kCFBooleanTrue,
			::kSecMatchLimitAll,
		};
	static auto query = ::CFDictionaryCreate(
		nullptr,
		keys,
		values,
		3,
		nullptr,
		nullptr
	);
	return query;
}


} // namespace


x509_store_loop::x509_store_loop () noexcept
{
	if (::SecItemCopyMatching(keychain_query(), (::CFTypeRef *)&store.ref) == ::errSecSuccess)
	{
		count = ::CFArrayGetCount(store.ref);
	}
}


x509 x509_store_loop::next () noexcept
{
	if (++index < count)
	{
		return (::SecCertificateRef)::CFRetain(
			::CFArrayGetValueAtIndex(store.ref, index)
		);
	}
	return {};
}


#elif __pal_os_windows //{{{1


x509 x509_store_loop::next () noexcept
{
	for (;;)
	{
		if (store)
		{
			if ((cert = ::CertEnumCertificatesInStore(store.ref, cert)) != nullptr)
			{
				return x509{inc_ref(cert)};
			}
		}

		if (sys_store_index < sys_store_count)
		{
			store = x509_store{::CertOpenSystemStore(NULL, sys_store[sys_store_index++])};
			cert = nullptr;
		}
		else
		{
			break;
		}
	}
	return {};
}


#endif //}}}1


} // namespace crypto::__bits


__pal_end
