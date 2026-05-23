#pragma once

/**
 * \file pal/crypto/hmac.hpp
 * Cryptographic keyed hashing (HMAC)
 */

#include <pal/crypto/concepts.hpp>
#include <pal/crypto/digest_algorithm.hpp>
#include <pal/buffer.hpp>
#include <pal/result.hpp>
#include <array>
#include <span>

namespace pal::crypto
{

/// One-way cryptographic keyed hashing using \a Algorithm.
///
/// For a complete, immediately available dataset use the static one_shot()
/// convenience method. For streaming data, call make() to create an instance,
/// feed data with one or more update() calls, then retrieve the digest with
/// finish(). The instance is reset internally after finish() and can be reused
/// with the same key.
template <digest_algorithm Algorithm>
class basic_hmac
{
public:

	/// Number of bytes in result digest
	static constexpr size_t digest_size = Algorithm::digest_size;

	/// Result digest type
	using digest_type = std::array<std::byte, digest_size>;

	/// Feed \a input to hasher. Returns *this for call chaining.
	basic_hmac &update (const_buffer auto const &input) noexcept
	{
		impl_.update(std::as_bytes(std::span{input}));
		return *this;
	}

	/// Calculate and return final digest. Resets the instance for reuse with the same key.
	digest_type finish () noexcept
	{
		digest_type digest{};
		impl_.finish(std::span{digest});
		return digest;
	}

	/// Write final digest into \a output and return view of written bytes.
	/// Returns error if \a output is smaller than digest_size.
	result<std::span<const std::byte, digest_size>> finish (mutable_buffer auto &output) noexcept
	{
		if (std::size(output) >= digest_size)
		{
			auto out = std::as_writable_bytes(std::span{output}).template first<digest_size>();
			impl_.finish(out);
			return std::as_bytes(out);
		}
		return make_unexpected(std::errc::no_buffer_space);
	}

	/// Create a hasher instance with \a key.
	static result<basic_hmac> make (const_buffer auto const &key) noexcept
	{
		std::error_code ec;
		if (basic_hmac h{std::as_bytes(std::span{key}), ec}; !ec)
		{
			return h;
		}
		return pal::unexpected{ec};
	}

	/// Compute digest of all \a inputs in a single step with \a key.
	static result<digest_type> one_shot (const_buffer auto const &key, const_buffer auto const &...inputs) noexcept
	{
		// clang-format off
		return make(key).transform([&] (auto h)
		{
			(h.update(inputs), ...);
			return h.finish();
		});
		// clang-format on
	}

private:

	Algorithm::hmac impl_;

	basic_hmac (std::span<const std::byte> key, std::error_code &ec) noexcept
		: impl_{key, ec}
	{
	}
};

/// \defgroup crypto_hmac Cryptographic keyed hashers (HMAC)
/// \{

/// MD5 HMAC (required for TURN long-term credential mechanism)
using md5_hmac = basic_hmac<algorithm::md5>;

/// SHA1 HMAC
using sha1_hmac = basic_hmac<algorithm::sha1>;

/// SHA256 HMAC
using sha256_hmac = basic_hmac<algorithm::sha256>;

/// SHA384 HMAC
using sha384_hmac = basic_hmac<algorithm::sha384>;

/// SHA512 HMAC
using sha512_hmac = basic_hmac<algorithm::sha512>;

/// \}

} // namespace pal::crypto
