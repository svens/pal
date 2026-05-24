#pragma once

/**
 * \file pal/crypto/certificate.hpp
 * X.509 public key certificate
 */

#include <pal/result.hpp>
#include <chrono>
#include <memory>
#include <span>
#include <string_view>

namespace pal::crypto
{

/// X.509 public key certificate wrapper.
///
/// Construction: use from_der() or from_pem().
/// All accessor methods are noexcept and remain valid for the lifetime of the
/// certificate object.
class certificate
{
public:

	/// Clock for certificate time-related properties.
	using clock_type = std::chrono::system_clock;

	/// Timestamp type for certificate time-related properties.
	using time_type = clock_type::time_point;

	/// Duration type for certificate time-related predicates.
	using duration = clock_type::duration;

	certificate () = default;

	/// Load X.509 certificate from DER-encoded \a der.
	template <typename Data>
	static result<certificate> from_der (const Data &der) noexcept
	{
		return import_der(std::as_bytes(std::span{der}));
	}

	/// Load X.509 certificate from PEM-encoded \a pem.
	template <typename Data>
	static result<certificate> from_pem (const Data &pem) noexcept
	{
		return import_pem({std::data(pem), std::size(pem)});
	}

	/// Returns true if this represents an unspecified (null) certificate.
	[[nodiscard]] bool is_null () const noexcept
	{
		return impl_ == nullptr;
	}

	/// Returns true if this is a valid (non-null) certificate.
	explicit operator bool () const noexcept
	{
		return !is_null();
	}

private:

	struct impl_type;
	using impl_ptr = std::shared_ptr<impl_type>;
	impl_ptr impl_;

	explicit certificate (impl_ptr impl) noexcept
		: impl_{std::move(impl)}
	{
	}

	static result<certificate> import_der (std::span<const std::byte> der) noexcept;
	static result<certificate> import_pem (std::string_view pem) noexcept;
};

} // namespace pal::crypto
