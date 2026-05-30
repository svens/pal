#include <pal/crypto/__crypto.hpp>

#if __pal_crypto_openssl

// clang-format off
#include <pal/crypto/__certificate.hpp>
#include <pal/crypto/digest_algorithm.hpp>
#include <pal/crypto/signature.hpp>
#include <pal/memory.hpp>
#include <openssl/evp.h>
#include <openssl/rsa.h>
// clang-format on

namespace pal::crypto
{

namespace
{

struct pkey_ctx_deleter
{
	void operator() (::EVP_PKEY_CTX *p) const noexcept
	{
		::EVP_PKEY_CTX_free(p);
	}
};
using pkey_ctx_ptr = std::unique_ptr<::EVP_PKEY_CTX, pkey_ctx_deleter>;

enum class operation_type : uint8_t
{
	sign,
	verify
};

} // namespace

namespace __signature
{

struct context
{
	key::impl_ptr op_key;
	pkey_ctx_ptr impl;
	size_t expected_signature_size;

	context (const key &k) noexcept
		: op_key{k.impl_}
		, impl{::EVP_PKEY_CTX_new(&op_key->pkey, nullptr)}
		, expected_signature_size{k.max_block_size()}
	{
	}

	[[nodiscard]] bool init (operation_type op) const noexcept
	{
		if (op == operation_type::sign)
		{
			return ::EVP_PKEY_private_check(impl.get()) == 1 && ::EVP_PKEY_sign_init(impl.get()) == 1;
		}
		return ::EVP_PKEY_private_check(impl.get()) != 1 && ::EVP_PKEY_verify_init(impl.get()) == 1;
	}

	[[nodiscard]] bool set_rsa_padding (std::string_view scheme_id) const noexcept
	{
		if (scheme_id == scheme::pkcs1::id)
		{
			return ::EVP_PKEY_CTX_set_rsa_padding(impl.get(), RSA_PKCS1_PADDING) == 1;
		}
		if (scheme_id == scheme::pss::id)
		{
			return ::EVP_PKEY_CTX_set_rsa_padding(impl.get(), RSA_PKCS1_PSS_PADDING) == 1
				&& ::EVP_PKEY_CTX_set_rsa_pss_saltlen(impl.get(), RSA_PSS_SALTLEN_DIGEST) == 1;
		}
		return false;
	}

	[[nodiscard]] bool set_digest (std::string_view digest_id) const noexcept
	{
		const ::EVP_MD *md = nullptr;
		if (digest_id == algorithm::sha1::id)
		{
			md = ::EVP_sha1();
		}
		else if (digest_id == algorithm::sha256::id)
		{
			md = ::EVP_sha256();
		}
		else if (digest_id == algorithm::sha384::id)
		{
			md = ::EVP_sha384();
		}
		else if (digest_id == algorithm::sha512::id)
		{
			md = ::EVP_sha512();
		}
		if (md == nullptr)
		{
			return false;
		}
		return ::EVP_PKEY_CTX_set_signature_md(impl.get(), md) == 1;
	}
};

void context_deleter::operator() (context *ctx) const noexcept
{
	delete ctx;
}

namespace
{

result<context_ptr>
make_context (const key &key, operation_type operation, std::string_view scheme_id, std::string_view digest_id) noexcept
{
	const bool is_rsa = key.algorithm() == key_algorithm::rsa;
	const bool is_ec = key.algorithm() == key_algorithm::ec;
	const bool wants_rsa = (scheme_id == scheme::pkcs1::id || scheme_id == scheme::pss::id);
	const bool wants_ec = (scheme_id == scheme::ecdsa::id);

	if ((wants_rsa && !is_rsa) || (wants_ec && !is_ec) || (!wants_rsa && !wants_ec))
	{
		return make_unexpected(std::errc::not_supported);
	}

	context_ptr ctx{new (std::nothrow) context{key}};
	if (!ctx || !ctx->impl)
	{
		return make_unexpected(std::errc::not_enough_memory);
	}

	if (!ctx->init(operation))
	{
		return make_unexpected(std::errc::not_supported);
	}

	// ECDSA signs raw digest bytes directly; padding and MD setup are RSA-only.
	if (wants_rsa && (!ctx->set_rsa_padding(scheme_id) || !ctx->set_digest(digest_id)))
	{
		return make_unexpected(std::errc::not_supported);
	}

	return ctx;
}

} // namespace

result<context_ptr> make_sign_context (const key &key, std::string_view scheme, std::string_view digest) noexcept
{
	return make_context(key, operation_type::sign, scheme, digest);
}

result<context_ptr> make_verify_context (const key &key, std::string_view scheme, std::string_view digest) noexcept
{
	return make_context(key, operation_type::verify, scheme, digest);
}

result<std::span<const std::byte>>
sign (const context_ptr &ctx, std::span<const std::byte> digest, std::span<std::byte> signature) noexcept
{
	if (signature.size() < ctx->expected_signature_size)
	{
		return make_unexpected(std::errc::no_buffer_space);
	}

	auto s_size = signature.size();
	auto *s_data = reinterpret_cast<uint8_t *>(signature.data());
	const auto *d_data = reinterpret_cast<const uint8_t *>(digest.data());
	const auto d_size = digest.size();

	const auto ret = ::EVP_PKEY_sign(ctx->impl.get(), s_data, &s_size, d_data, d_size);
	if (ret == 1)
	{
		return signature.first(s_size);
	}
	if (ret == -2)
	{
		return make_unexpected(std::errc::not_supported);
	}
	return make_unexpected(std::errc::invalid_argument);
}

bool is_valid (const context_ptr &ctx, std::span<const std::byte> digest, std::span<const std::byte> signature) noexcept
{
	const auto *s_data = reinterpret_cast<const uint8_t *>(signature.data());
	const auto s_size = signature.size();
	const auto *d_data = reinterpret_cast<const uint8_t *>(digest.data());
	const auto d_size = digest.size();

	return ::EVP_PKEY_verify(ctx->impl.get(), s_data, s_size, d_data, d_size) == 1;
}

} // namespace __signature

} // namespace pal::crypto

#endif // __pal_crypto_openssl
