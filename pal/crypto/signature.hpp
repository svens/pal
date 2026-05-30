#pragma once

/**
 * \file pal/crypto/signature.hpp
 * Digital signature provider and verifier
 *
 * Both provider and verifier operate on pre-computed message digests —
 * computing the digest is the caller's responsibility.
 */

#include <pal/crypto/concepts.hpp>
#include <pal/crypto/key.hpp>
#include <pal/buffer.hpp>
#include <pal/result.hpp>
#include <array>
#include <memory>
#include <span>
#include <string_view>

namespace pal::crypto
{

/// Signature scheme tags
namespace scheme
{

/// RSA PKCS#1 v1.5 — RSA keys only, deterministic
struct pkcs1
{
	static constexpr std::string_view id = "pkcs1";
	static constexpr bool is_deterministic = true;
};

/// RSA PSS — RSA keys only, non-deterministic (random salt)
struct pss
{
	static constexpr std::string_view id = "pss";
	static constexpr bool is_deterministic = false;
};

/// ECDSA — EC keys only, non-deterministic (random nonce)
struct ecdsa
{
	static constexpr std::string_view id = "ecdsa";
	static constexpr bool is_deterministic = false;
};

} // namespace scheme

namespace __signature
{

// clang-format off

struct context;
struct context_deleter { void operator() (context *) const noexcept; };
using context_ptr = std::unique_ptr<context, context_deleter>;

result<context_ptr> make_sign_context (
	const key &key,
	std::string_view scheme,
	std::string_view digest
) noexcept;

result<context_ptr> make_verify_context (
	const key &key,
	std::string_view scheme,
	std::string_view digest
) noexcept;

result<std::span<const std::byte>> sign (
	const context_ptr &ctx,
	std::span<const std::byte> digest,
	std::span<std::byte> signature
) noexcept;

bool is_valid (
	const context_ptr &ctx,
	std::span<const std::byte> digest,
	std::span<const std::byte> signature
) noexcept;

// clang-format on

} // namespace __signature

/// Digital signature operations using \a Scheme and \a Digest.
template <signature_scheme Scheme, digest_algorithm Digest>
class signature
{
public:

	signature () = delete;

	/// Signature scheme type
	using scheme_type = Scheme;

	/// Pre-computed digest type — matches basic_hash<Digest>::digest_type
	using digest_type = std::array<std::byte, Digest::digest_size>;

	/// Signs pre-computed digests using a private key.
	class provider;

	/// Verifies signatures against pre-computed digests using a public key.
	class verifier;
};

template <signature_scheme Scheme, digest_algorithm Digest>
class signature<Scheme, Digest>::provider
{
public:

	/// Returns the output size of sign() in bytes. Exact for RSA, upper bound for ECDSA.
	[[nodiscard]] size_t signature_size () const noexcept
	{
		return size_;
	}

	/// Sign \a digest into \a signature. Returns span of bytes written.
	[[nodiscard]] result<std::span<const std::byte>>
	sign (const digest_type &digest, mutable_buffer auto &signature) noexcept
	{
		return __signature::sign(
			context_, std::as_bytes(std::span{digest}), std::as_writable_bytes(std::span{signature})
		);
	}

	/// Create provider from \a key (must be a private key matching \a Scheme's algorithm).
	static result<provider> make (const key &key) noexcept
	{
		return __signature::make_sign_context(key, Scheme::id, Digest::id)
			.transform([&key] (auto ctx) { return provider{std::move(ctx), key.max_block_size()}; });
	}

private:

	__signature::context_ptr context_;
	const size_t size_;

	provider (__signature::context_ptr &&ctx, size_t size) noexcept
		: context_{std::move(ctx)}
		, size_{size}
	{
	}
};

template <signature_scheme Scheme, digest_algorithm Digest>
class signature<Scheme, Digest>::verifier
{
public:

	/// Returns the expected signature size in bytes. Exact for RSA, upper bound for ECDSA.
	[[nodiscard]] size_t signature_size () const noexcept
	{
		return size_;
	}

	/// Return true if \a signature is valid for \a digest.
	[[nodiscard]] bool is_valid (const digest_type &digest, const_buffer auto const &signature) noexcept
	{
		return __signature::is_valid(
			context_, std::as_bytes(std::span{digest}), std::as_bytes(std::span{signature})
		);
	}

	/// Create verifier from \a key (must be a public key matching \a Scheme's algorithm).
	static result<verifier> make (const key &key) noexcept
	{
		return __signature::make_verify_context(key, Scheme::id, Digest::id)
			.transform([&key] (auto ctx) { return verifier{std::move(ctx), key.max_block_size()}; });
	}

private:

	__signature::context_ptr context_;
	const size_t size_;

	verifier (__signature::context_ptr &&ctx, size_t size) noexcept
		: context_{std::move(ctx)}
		, size_{size}
	{
	}
};

} // namespace pal::crypto
