#include <pal/crypto/__crypto.hpp>

#if __pal_crypto_openssl

#include <pal/crypto/__certificate.hpp>
#include <pal/memory.hpp>
#include <openssl/pkcs12.h>
#include <cstdlib>
#include <filesystem>

namespace pal::crypto
{

namespace
{

namespace fs = std::filesystem;

struct pkcs12_deleter
{
	void operator() (::PKCS12 *p) const noexcept
	{
		::PKCS12_free(p);
	}
};
using pkcs12_ptr = std::unique_ptr<::PKCS12, pkcs12_deleter>;

struct x509_stack_deleter
{
	void operator() (STACK_OF(X509) * p) const noexcept
	{
		::sk_X509_pop_free(p, ::X509_free);
	}
};
using x509_stack_ptr = std::unique_ptr<STACK_OF(X509), x509_stack_deleter>;

} // namespace

result<certificate::impl_ptr> certificate_store::import_pkcs12_chain (std::span<const std::byte> data) noexcept
{
	const auto *ptr = reinterpret_cast<const uint8_t *>(data.data());
	const pkcs12_ptr p12{::d2i_PKCS12(nullptr, &ptr, static_cast<long>(data.size()))};
	if (!p12)
	{
		return make_unexpected(std::errc::invalid_argument);
	}

	::X509 *leaf_raw = nullptr;
	::EVP_PKEY *pkey_raw = nullptr;
	STACK_OF(X509) *chain_raw = nullptr;

	if (::PKCS12_parse(p12.get(), nullptr, &pkey_raw, &leaf_raw, &chain_raw) != 1)
	{
		return make_unexpected(std::errc::invalid_argument);
	}

	cert_ptr leaf{leaf_raw};
	pkey_ptr pkey{pkey_raw};
	const x509_stack_ptr chain{chain_raw};

	if (!leaf)
	{
		return make_unexpected(std::errc::invalid_argument);
	}

	auto head = pal::make_shared<certificate::impl_type>(std::move(leaf));
	if (!head)
	{
		return pal::unexpected{head.error()};
	}

	auto tail = *head;
	while (sk_X509_num(chain.get()) > 0)
	{
		auto node = pal::make_shared<certificate::impl_type>(cert_ptr{sk_X509_pop(chain.get())});
		if (!node)
		{
			return pal::unexpected{node.error()};
		}
		tail->next = std::move(*node);
		tail = tail->next;
	}

	(*head)->private_key = std::move(pkey);

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
	try
	{
		if (const char *env = std::getenv("SSL_CERT_DIR"); env != nullptr)
		{
			return from_cert_dir(env);
		}

		std::error_code ec;
		if (auto dir = fs::current_path(ec); !ec)
		{
			return from_cert_dir(dir);
		}
		return pal::unexpected{ec};
	}
	catch (...)
	{
		return make_unexpected(std::errc::not_enough_memory);
	}
}

} // namespace pal::crypto

#endif // __pal_crypto_openssl
