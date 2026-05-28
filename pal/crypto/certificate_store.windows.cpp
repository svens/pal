#include <pal/crypto/__crypto.hpp>

#if __pal_crypto_windows

// clang-format off
#include <pal/crypto/__certificate.hpp>
#include <pal/memory.hpp>
#include <ncrypt.h>
// clang-format on

namespace pal::crypto
{

namespace
{

struct store_deleter
{
	void operator() (::HCERTSTORE p) const noexcept
	{
		::CertCloseStore(p, 0);
	}
};
using store_ptr = std::unique_ptr<void, store_deleter>;

store_ptr open_pfx (std::span<const std::byte> data) noexcept
{
	::CRYPT_DATA_BLOB pfx{};
	pfx.cbData = static_cast<::DWORD>(data.size());
	pfx.pbData = reinterpret_cast<::BYTE *>(const_cast<std::byte *>(data.data()));

	// PKCS12_NO_PERSIST_KEY does not set CERT_KEY_PROV_INFO_PROP_ID on the cert context,
	// so CryptAcquireCertificatePrivateKey always fails with NTE_NO_KEY. Import with flag=0
	// instead: key is persisted to disk, acquired eagerly below, and deleted from disk in
	// certificate::impl_type::~impl_type via NCryptDeleteKey.
	return store_ptr{::PFXImportCertStore(&pfx, L"", 0)};
}

} // namespace

result<certificate_store> certificate_store::import_pkcs12 (std::span<const std::byte> data) noexcept
{
	const auto pfx = open_pfx(data);
	if (!pfx)
	{
		return make_unexpected(std::errc::invalid_argument);
	}

	const auto store = static_cast<::HCERTSTORE>(pfx.get());

	const cert_ptr leaf{::CertFindCertificateInStore(
		store, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0, CERT_FIND_HAS_PRIVATE_KEY, nullptr, nullptr
	)};
	if (!leaf)
	{
		return make_unexpected(std::errc::invalid_argument);
	}

	auto head = pal::make_shared<certificate::impl_type>(cert_ptr{::CertDuplicateCertificateContext(leaf.get())});
	if (!head)
	{
		return pal::unexpected{head.error()};
	}

	::HCRYPTPROV_OR_NCRYPT_KEY_HANDLE pkey = 0;
	::DWORD key_spec = 0;
	const auto ok = ::CryptAcquireCertificatePrivateKey(
		leaf.get(), CRYPT_ACQUIRE_ONLY_NCRYPT_KEY_FLAG, nullptr, &pkey, &key_spec, nullptr
	);
	if (!ok || key_spec != CERT_NCRYPT_KEY_SPEC)
	{
		return make_unexpected(std::errc::invalid_argument);
	}
	(*head)->private_key = static_cast<::NCRYPT_KEY_HANDLE>(pkey);

	auto tail = *head;
	::PCCERT_CONTEXT it = nullptr;
	while ((it = ::CertEnumCertificatesInStore(store, it)) != nullptr)
	{
		if (::CertCompareCertificate(X509_ASN_ENCODING, it->pCertInfo, leaf->pCertInfo))
		{
			continue;
		}
		auto node = pal::make_shared<certificate::impl_type>(cert_ptr{::CertDuplicateCertificateContext(it)});
		if (!node)
		{
			return pal::unexpected{node.error()};
		}
		tail->next = std::move(*node);
		tail = tail->next;
	}

	return pal::make_shared<certificate_store::impl_type>(std::move(*head)).transform(certificate_store::to_api);
}

void certificate_store::advance (certificate &cert) noexcept
{
	cert.impl_ = cert.impl_->next;
}

} // namespace pal::crypto

#endif // __pal_crypto_windows
