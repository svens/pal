#pragma once

/**
 * \file pal/crypto/certificate_filter.hpp
 * Predicate factories for filtering certificates.
 *
 * Each function returns a callable matching std::predicate<const certificate &>,
 * suitable for std::ranges::find_if, std::views::filter, and certificate_store::find_first.
 *
 * Recommended usage:
 *   namespace cf = pal::crypto::certificate_filter;
 *   auto cert = store.find_first(cf::with_common_name("example.com"));
 *
 * \warning String-valued predicates (with_common_name, with_thumbprint, with_fqdn, with_ip)
 * capture their argument as std::string_view — the caller is responsible for ensuring the
 * referenced storage outlives the predicate. Safe for string literals, member strings on
 * long-lived config objects, and arguments passed directly into find_first().
 * UNSAFE for temporaries, e.g. `cf::with_common_name(std::string{"x"})` produces a dangling view.
 */

#include <pal/crypto/certificate.hpp>
#include <pal/crypto/alternative_name_value.hpp>
#include <string_view>
#include <utility>

namespace pal::crypto::certificate_filter
{

/// Match certificates whose Common Name equals \a name exactly.
/// \warning \a name must outlive the returned predicate.
inline auto with_common_name (std::string_view name) noexcept
{
	return [name] (const certificate &c) noexcept
	{
		return c.common_name() == name;
	};
}

/// Match certificates whose SHA1 thumbprint equals \a hex exactly.
///
/// Strict comparison: \a hex must be canonical lowercase hex with no separators
/// (e.g. "b37f329850a231675794fddacc100eb435902c45"). Any other format will not
/// match.
/// \warning \a hex must outlive the returned predicate.
inline auto with_thumbprint (std::string_view hex) noexcept
{
	return [hex] (const certificate &c) noexcept
	{
		return c.fingerprint() == hex;
	};
}

/// Match certificates whose Subject Alternative Name contains an FQDN matching \a host.
///
/// Uses TLS-style wildcard semantics: a SAN entry of "*.example.com" matches
/// "sub.example.com" but not "a.b.example.com".
/// \warning \a host must outlive the returned predicate.
inline auto with_fqdn (std::string_view host) noexcept
{
	return [host] (const certificate &c) noexcept
	{
		return c.subject_alternative_name_value().contains(host);
	};
}

/// Match certificates whose Subject Alternative Name contains the IP address \a addr.
/// \warning \a addr must outlive the returned predicate.
inline auto with_ip (std::string_view addr) noexcept
{
	return [addr] (const certificate &c) noexcept
	{
		return c.subject_alternative_name_value().contains(addr);
	};
}

/// Match certificates that have an associated private key.
inline auto has_private_key () noexcept
{
	return [] (const certificate &c) noexcept
	{
		return c.private_key().has_value();
	};
}

/// Match certificates that are self-signed (Subject == Issuer).
inline auto is_self_signed () noexcept
{
	return [] (const certificate &c) noexcept
	{
		return c.is_self_signed();
	};
}

/// Match certificates that are valid (not expired) at \a when.
///
/// The default captures the time at predicate construction; reusing the same
/// predicate across long-running iteration sees no clock drift.
inline auto not_expired_at (certificate::time_type when = certificate::clock_type::now()) noexcept
{
	return [when] (const certificate &c) noexcept
	{
		return c.not_expired_at(when);
	};
}

/// Match certificates issued by \a issuer.
///
/// The issuer is captured by value (certificate is a cheap shared_ptr handle).
inline auto is_issued_by (certificate issuer) noexcept
{
	return [issuer = std::move(issuer)] (const certificate &c) noexcept
	{
		return c.is_issued_by(issuer);
	};
}

} // namespace pal::crypto::certificate_filter
