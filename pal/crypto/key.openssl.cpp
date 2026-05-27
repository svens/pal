#include <pal/crypto/__crypto.hpp>

#if __pal_crypto_openssl

// clang-format off
#include <pal/crypto/__certificate.hpp>
#include <pal/memory.hpp>
// clang-format on

namespace pal::crypto
{

namespace
{

key_algorithm to_algorithm (int id) noexcept
{
	if (id == EVP_PKEY_RSA)
	{
		return key_algorithm::rsa;
	}
	if (id == EVP_PKEY_EC)
	{
		return key_algorithm::ec;
	}
	return key_algorithm::opaque;
}

} // namespace

key::impl_type::impl_type (certificate::impl_ptr owner, ::EVP_PKEY &pkey) noexcept
	: owner{std::move(owner)}
	, pkey{pkey}
	, algorithm{to_algorithm(::EVP_PKEY_get_base_id(&pkey))}
	, size_bits{static_cast<size_t>(::EVP_PKEY_bits(&pkey))}
	, max_block_size{static_cast<size_t>(::EVP_PKEY_size(&pkey))}
{
}

key_algorithm key::algorithm () const noexcept
{
	return impl_->algorithm;
}

size_t key::size_bits () const noexcept
{
	return impl_->size_bits;
}

size_t key::max_block_size () const noexcept
{
	return impl_->max_block_size;
}

result<key> certificate::public_key () const noexcept
{
	auto &pkey = *::X509_get0_pubkey(impl_->x509.get());
	return pal::make_shared<key::impl_type>(impl_, pkey).transform(key::to_api);
}

} // namespace pal::crypto

#endif // __pal_crypto_openssl
