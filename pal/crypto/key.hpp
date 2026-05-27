#pragma once

/**
 * \file pal/crypto/key.hpp
 * Asymmetric key
 */

#include <cstddef>
#include <memory>

namespace pal::crypto
{

class certificate;
class certificate_store;

/// Key algorithm
enum class key_algorithm
{
	opaque, ///< Unrecognised or unsupported algorithm
	rsa,	///< RSA
	ec,	///< Elliptic curve
};

/// Certificate public or private key.
class key
{
public:

	key () = default;

	/// Returns true if this represents an unspecified (null) key.
	[[nodiscard]] bool is_null () const noexcept
	{
		return impl_ == nullptr;
	}

	/// Returns true if this is a valid (non-null) key.
	explicit operator bool () const noexcept
	{
		return !is_null();
	}

	/// Return key algorithm.
	[[nodiscard]] key_algorithm algorithm () const noexcept;

	/// Return number of bits in this key.
	[[nodiscard]] size_t size_bits () const noexcept;

	/// Return maximum output buffer size for cryptographic operations with this key.
	[[nodiscard]] size_t max_block_size () const noexcept;

private:

	struct impl_type;
	using impl_ptr = std::shared_ptr<impl_type>;
	impl_ptr impl_;

	explicit key (impl_ptr impl) noexcept
		: impl_{std::move(impl)}
	{
	}

	static key to_api (impl_ptr impl) noexcept;

	friend class certificate;
	friend class certificate_store;
};

inline key key::to_api (impl_ptr impl) noexcept
{
	return key{std::move(impl)};
}

} // namespace pal::crypto
