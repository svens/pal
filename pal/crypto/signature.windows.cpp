#include <pal/crypto/__crypto.hpp>

#if __pal_crypto_windows

#include <pal/crypto/__certificate.hpp>
#include <pal/crypto/digest_algorithm.hpp>
#include <pal/crypto/signature.hpp>
#include <pal/memory.hpp>
#include <bcrypt.h>
#include <ncrypt.h>

namespace pal::crypto
{

namespace __signature
{

namespace
{

enum class operation_type : uint8_t
{
	sign,
	verify
};

} // namespace

struct context
{
	key::impl_ptr op_key;
	std::variant<std::monostate, ::BCRYPT_PKCS1_PADDING_INFO, ::BCRYPT_PSS_PADDING_INFO> storage{};
	const std::pair<void *, DWORD> padding;
	const size_t expected_signature_size;

	context (const key &k, std::string_view scheme_id, std::string_view digest_id) noexcept
		: op_key{k.impl_}
		, padding{setup_padding(scheme_id, digest_id, storage)}
		, expected_signature_size{k.max_block_size()}
	{
	}

	[[nodiscard]] bool init (operation_type op) const noexcept
	{
		if (op == operation_type::sign)
		{
			return std::holds_alternative<::NCRYPT_KEY_HANDLE>(op_key->pkey);
		}
		return std::holds_alternative<::BCRYPT_KEY_HANDLE>(op_key->pkey);
	}

	[[nodiscard]] static std::pair<LPCWSTR, size_t> map_digest (std::string_view digest_id) noexcept
	{
		if (digest_id == algorithm::sha1::id)
		{
			return {NCRYPT_SHA1_ALGORITHM, algorithm::sha1::digest_size};
		}
		if (digest_id == algorithm::sha256::id)
		{
			return {NCRYPT_SHA256_ALGORITHM, algorithm::sha256::digest_size};
		}
		if (digest_id == algorithm::sha384::id)
		{
			return {NCRYPT_SHA384_ALGORITHM, algorithm::sha384::digest_size};
		}
		if (digest_id == algorithm::sha512::id)
		{
			return {NCRYPT_SHA512_ALGORITHM, algorithm::sha512::digest_size};
		}
		return {nullptr, 0};
	}

	[[nodiscard]] static std::pair<void *, DWORD>
	setup_padding (std::string_view scheme_id, std::string_view digest_id, decltype(storage) &s) noexcept
	{
		auto [alg, salt_size] = map_digest(digest_id);
		if (alg == nullptr)
		{
			return {nullptr, 0};
		}
		if (scheme_id == scheme::pkcs1::id)
		{
			auto &info = s.emplace<::BCRYPT_PKCS1_PADDING_INFO>();
			info.pszAlgId = alg;
			return {&info, BCRYPT_PAD_PKCS1};
		}
		if (scheme_id == scheme::pss::id)
		{
			auto &info = s.emplace<::BCRYPT_PSS_PADDING_INFO>();
			info.pszAlgId = alg;
			info.cbSalt = static_cast<ULONG>(salt_size);
			return {&info, BCRYPT_PAD_PSS};
		}
		return {nullptr, 0};
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

	context_ptr ctx{new (std::nothrow) context{key, scheme_id, digest_id}};
	if (!ctx)
	{
		return make_unexpected(std::errc::not_enough_memory);
	}

	if (!ctx->init(operation))
	{
		return make_unexpected(std::errc::not_supported);
	}

	// ECDSA signs raw digest bytes directly; padding setup is RSA-only.
	if (wants_rsa && !ctx->padding.first)
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

	DWORD cbResult = 0;
	const auto status = ::NCryptSignHash(
		*std::get_if<::NCRYPT_KEY_HANDLE>(&ctx->op_key->pkey),
		ctx->padding.first,
		reinterpret_cast<PBYTE>(const_cast<std::byte *>(digest.data())),
		static_cast<DWORD>(digest.size()),
		reinterpret_cast<PBYTE>(signature.data()),
		static_cast<DWORD>(signature.size()),
		&cbResult,
		ctx->padding.second
	);

	if (status == ERROR_SUCCESS)
	{
		return signature.first(cbResult);
	}
	if (status == NTE_BUFFER_TOO_SMALL)
	{
		return make_unexpected(std::errc::no_buffer_space);
	}
	return make_unexpected(std::errc::invalid_argument);
}

bool is_valid (const context_ptr &ctx, std::span<const std::byte> digest, std::span<const std::byte> signature) noexcept
{
	const auto status = ::BCryptVerifySignature(
		*std::get_if<::BCRYPT_KEY_HANDLE>(&ctx->op_key->pkey),
		ctx->padding.first,
		reinterpret_cast<PUCHAR>(const_cast<std::byte *>(digest.data())),
		static_cast<ULONG>(digest.size()),
		reinterpret_cast<PUCHAR>(const_cast<std::byte *>(signature.data())),
		static_cast<ULONG>(signature.size()),
		ctx->padding.second
	);
	return status == STATUS_SUCCESS;
}

} // namespace __signature

} // namespace pal::crypto

#endif // __pal_crypto_windows
