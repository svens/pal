#pragma once

/**
 * \file pal/crypto/certificate.hpp
 * X.509 public key certificate
 */

#include <pal/result.hpp>
#include <chrono>
#include <cstdint>
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

	/// Return certificate encoded as DER blob.
	[[nodiscard]] std::span<const std::byte> as_bytes () const noexcept;

	/// Return X.509 structure version (1 for v1, 3 for v3).
	[[nodiscard]] int version () const noexcept;

	/// Return certificate serial number as big-endian byte span.
	[[nodiscard]] std::span<const uint8_t> serial_number () const noexcept;

	/// Return Common Name from the certificate's Subject Distinguished Name.
	[[nodiscard]] std::string_view common_name () const noexcept;

	/// Return SHA1 fingerprint as lowercase hex string.
	[[nodiscard]] std::string_view fingerprint () const noexcept;

	/// Return timestamp from which this certificate is valid.
	[[nodiscard]] time_type not_before () const noexcept;

	/// Return timestamp until which this certificate is valid.
	[[nodiscard]] time_type not_after () const noexcept;

	/// Return true if this certificate is valid at \a when.
	[[nodiscard]] bool not_expired_at (time_type when) const noexcept
	{
		return not_before() <= when && when <= not_after();
	}

	/// Return true if this certificate remains valid for \a d starting from \a when.
	[[nodiscard]] bool not_expired_for (duration d, time_type when = clock_type::now()) const noexcept
	{
		return not_expired_at(when + d);
	}

	/// Return true if \a this certificate was issued by \a that certificate.
	[[nodiscard]] bool is_issued_by (const certificate &that) const noexcept;

	/// Return true if this certificate was issued by itself.
	[[nodiscard]] bool is_self_signed () const noexcept;

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
