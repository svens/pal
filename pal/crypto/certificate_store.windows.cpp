#include <pal/crypto/__crypto.hpp>

#if __pal_crypto_windows

#include <pal/crypto/__certificate.hpp>
#include <pal/memory.hpp>
#include <ncrypt.h>

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

store_ptr open_my_store () noexcept
{
	return store_ptr{::CertOpenStore(
		CERT_STORE_PROV_SYSTEM_W,
		0,
		0,
		CERT_SYSTEM_STORE_CURRENT_USER | CERT_STORE_READONLY_FLAG | CERT_STORE_OPEN_EXISTING_FLAG,
		L"MY"
	)};
}

::NCRYPT_KEY_HANDLE acquire_borrowed_key (::PCCERT_CONTEXT cert, ::BOOL &caller_must_free) noexcept
{
	::HCRYPTPROV_OR_NCRYPT_KEY_HANDLE pkey = 0;
	::DWORD key_spec = 0;
	const auto flags = CRYPT_ACQUIRE_ONLY_NCRYPT_KEY_FLAG | CRYPT_ACQUIRE_SILENT_FLAG;
	if (!::CryptAcquireCertificatePrivateKey(cert, flags, nullptr, &pkey, &key_spec, &caller_must_free))
	{
		return 0;
	}

	if (key_spec != CERT_NCRYPT_KEY_SPEC)
	{
		return 0;
	}

	return static_cast<::NCRYPT_KEY_HANDLE>(pkey);
}

} // namespace

result<certificate::impl_ptr> certificate_store::import_pkcs12_chain (std::span<const std::byte> data) noexcept
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
	::BOOL caller_must_free = FALSE;
	const auto ok = ::CryptAcquireCertificatePrivateKey(
		leaf.get(), CRYPT_ACQUIRE_ONLY_NCRYPT_KEY_FLAG, nullptr, &pkey, &key_spec, &caller_must_free
	);
	if (!ok || key_spec != CERT_NCRYPT_KEY_SPEC)
	{
		return make_unexpected(std::errc::invalid_argument);
	}
	(*head)->private_key = static_cast<::NCRYPT_KEY_HANDLE>(pkey);
	(*head)->free_private_key_on_destruct = (caller_must_free == TRUE);

	// PKCS#12 was imported with flag=0, which persists the key to disk; the dtor must wipe it. See open_pfx
	(*head)->delete_private_key_on_destruct = true;

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

	return *head;
}

result<certificate_store> certificate_store::import_pkcs12 (std::span<const std::byte> data) noexcept
{
	// clang-format off

	return import_pkcs12_chain(data).and_then([] (certificate::impl_ptr head) -> result<certificate_store>
	{
		return pal::make_shared<certificate_store::impl_type>(std::move(head))
			.transform(certificate_store::to_api);
	});

	// clang-format on
}

void certificate_store::advance (certificate &cert) noexcept
{
	cert.impl_ = cert.impl_->next;
}

result<certificate_store> certificate_store::from_user_identities () noexcept
{
	const auto my = open_my_store();
	if (!my)
	{
		return make_unexpected(std::errc::invalid_argument);
	}

	const auto store = static_cast<::HCERTSTORE>(my.get());

	certificate::impl_ptr head;
	certificate::impl_ptr tail;

	::PCCERT_CONTEXT it = nullptr;
	while ((it = ::CertEnumCertificatesInStore(store, it)) != nullptr)
	{
		auto node = pal::make_shared<certificate::impl_type>(cert_ptr{::CertDuplicateCertificateContext(it)});
		if (!node)
		{
			return pal::unexpected{node.error()};
		}

		::BOOL caller_must_free = FALSE;
		if (auto pkey = acquire_borrowed_key(it, caller_must_free); pkey != 0)
		{
			(*node)->private_key = pkey;
			(*node)->free_private_key_on_destruct = (caller_must_free == TRUE);

			// delete_private_key_on_destruct stays false: the key belongs to
			// the user's MY store and must not be wiped from disk.
		}

		if (!head)
		{
			head = *node;
		}
		else
		{
			tail->next = *node;
		}
		tail = *node;
	}

	return pal::make_shared<certificate_store::impl_type>(std::move(head)).transform(certificate_store::to_api);
}

} // namespace pal::crypto

#endif // __pal_crypto_windows
