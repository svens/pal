#pragma once

/**
 * \file pal/crypto/certificate_store.hpp
 * X.509 certificate store
 */

#include <pal/buffer.hpp>
#include <pal/crypto/certificate.hpp>
#include <pal/result.hpp>
#include <algorithm>
#include <concepts>
#include <filesystem>
#include <iterator>
#include <span>

namespace pal::crypto
{

/// Forward-iterable collection of X.509 certificates loaded from a PKCS#12 source.
/// The leaf certificate (first in iteration order) has private_key() available.
class certificate_store
{
public:

	certificate_store () noexcept = default;

	/// Load an identity from a PKCS#12 blob in memory.
	///
	/// The PKCS#12 must contain a leaf certificate with a private key; the leaf
	/// is placed first in the store and is the only certificate for which
	/// private_key() succeeds. Returns std::errc::invalid_argument if the blob
	/// is malformed or contains no identity (certs-only PKCS#12 is rejected —
	/// use from_system() to load trust anchors).
	static result<certificate_store> from_pkcs12 (const_buffer auto const &pkcs12) noexcept
	{
		return import_pkcs12(std::as_bytes(std::span{pkcs12}));
	}

	/// Load an identity from a PKCS#12 file at \a path.
	/// \copydetails from_pkcs12(const_buffer auto const &)
	static result<certificate_store> from_pkcs12 (const std::filesystem::path &path) noexcept;

	/// Load all certificates from PKCS#12 files in directory \a dir.
	///
	/// Non-recursive enumeration of \a dir. Files whose extension (lower-cased)
	/// is `.pfx` or `.p12` are parsed; everything else is ignored. Per-file
	/// parse / read failures are skipped silently. Entries are loaded in
	/// lexicographic order of `filename()`, file-by-file (each file's leaf
	/// followed by its chain).
	///
	/// Returns a valid (possibly empty) store on success. Returns an error
	/// only when \a dir itself cannot be opened, or on out-of-memory.
	static result<certificate_store> from_cert_dir (const std::filesystem::path &dir) noexcept;

	/// Load the current user's personal identity store (certificates with private keys).
	///
	/// - Windows: `CERT_SYSTEM_STORE_CURRENT_USER\MY`.
	/// - OpenSSL: equivalent to `from_cert_dir($SSL_CERT_DIR)`, falling back
	///   to the current working directory if `SSL_CERT_DIR` is unset.
	static result<certificate_store> from_user_identities () noexcept;

	/// Returns true if this represents an unspecified (null) store.
	[[nodiscard]] bool is_null () const noexcept
	{
		return impl_ == nullptr;
	}

	/// Returns true if this is a valid (non-null) store.
	explicit operator bool () const noexcept
	{
		return !is_null();
	}

	/// Returns true if the store is null or contains no certificates.
	[[nodiscard]] bool empty () const noexcept;

	class const_iterator;

	/// Return iterator to first certificate. Dereferencing begin() == end() is undefined.
	[[nodiscard]] const_iterator begin () const noexcept;

	/// Return past-the-end sentinel.
	[[nodiscard]] static std::default_sentinel_t end () noexcept
	{
		return {};
	}

	/// Return the first certificate satisfying \a pred, or a null certificate if none match.
	///
	/// For multi-match use cases, compose with the standard library:
	/// \code
	/// for (const auto &c: store | std::views::filter(pred)) ...
	/// \endcode
	template <std::predicate<const certificate &> Pred>
	[[nodiscard]] certificate
	find_first (Pred pred) const noexcept(noexcept(pred(std::declval<const certificate &>())))
	{
		const auto it = std::ranges::find_if(*this, std::move(pred));
		return it == end() ? certificate{} : *it;
	}

private:

	struct impl_type;
	using impl_ptr = std::shared_ptr<impl_type>;
	impl_ptr impl_;

	explicit certificate_store (impl_ptr impl) noexcept
		: impl_{std::move(impl)}
	{
	}

	static result<certificate_store> import_pkcs12 (std::span<const std::byte> data) noexcept;
	static result<certificate::impl_ptr> import_pkcs12_chain (std::span<const std::byte> data) noexcept;
	static result<certificate::impl_ptr> load_one (const std::filesystem::path &path) noexcept;
	static void advance (certificate &cert) noexcept;
	static certificate_store to_api (impl_ptr impl) noexcept;
};

/// Forward iterator over the certificates in a certificate_store.
class certificate_store::const_iterator
{
public:

	/// \cond
	using iterator_concept = std::forward_iterator_tag;
	using iterator_category = std::input_iterator_tag;
	using value_type = certificate;
	using pointer = const certificate *;
	using reference = const certificate &;
	using difference_type = std::ptrdiff_t;
	/// \endcond

	const_iterator () = default;
	const_iterator (const const_iterator &) = default;
	const_iterator &operator= (const const_iterator &) = default;

	[[nodiscard]] bool operator== (const const_iterator &that) const noexcept
	{
		return entry_.impl_ == that.entry_.impl_;
	}

	[[nodiscard]] bool operator== (std::default_sentinel_t) const noexcept
	{
		return entry_.impl_ == nullptr;
	}

	const_iterator &operator++ () noexcept
	{
		certificate_store::advance(entry_);
		return *this;
	}

	const_iterator operator++ (int) noexcept
	{
		auto copy = *this;
		++(*this);
		return copy;
	}

	[[nodiscard]] reference operator* () const noexcept
	{
		return entry_;
	}

	[[nodiscard]] pointer operator->() const noexcept
	{
		return &entry_;
	}

private:

	certificate entry_{};

	explicit const_iterator (certificate::impl_ptr impl) noexcept
		: entry_{certificate::to_api(std::move(impl))}
	{
	}

	friend class certificate_store;
};

inline certificate_store certificate_store::to_api (impl_ptr impl) noexcept
{
	return certificate_store{std::move(impl)};
}

} // namespace pal::crypto
