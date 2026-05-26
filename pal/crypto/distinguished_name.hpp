#pragma once

/**
 * \file pal/crypto/distinguished_name.hpp
 * X.509 certificate distinguished name
 */

#include <array>
#include <charconv>
#include <format>
#include <iterator>
#include <memory>
#include <string_view>

namespace pal::crypto
{

class certificate;

/// Sequence of entries describing identifying information in a certificate
/// (issuer or subject distinguished name).
class distinguished_name
{
public:

	class const_iterator;

	/// Single entry in a distinguished name: an OID and its associated value.
	/// Both string_views remain valid until the next iterator advancement or
	/// iterator destruction — do not hold them past operator++().
	struct entry
	{
		std::string_view oid;
		std::string_view value;
	};

	/// Returns iterator to the first entry.
	[[nodiscard]] const_iterator begin () const noexcept;

	/// Returns sentinel representing one-past-the-end.
	[[nodiscard]] static std::default_sentinel_t end () noexcept;

	/// Format distinguished name as RFC 4514-style string into [first, last).
	/// Entries are ordered most-significant first, separated by ", ".
	/// Values containing special characters are not escaped; callers should
	/// treat the result as informational rather than a strict RFC 4514 encoding.
	[[nodiscard]] std::to_chars_result to_chars (char *first, char *last) const noexcept;

private:

	struct impl_type;
	using impl_ptr = std::shared_ptr<impl_type>;
	impl_ptr impl_;

	static constexpr size_t max_oid_size = 64;
	using oid_buffer = std::array<char, max_oid_size>;

	static constexpr size_t max_value_size = 136;
	using value_buffer = std::array<char, max_value_size>;

	explicit distinguished_name (impl_ptr impl) noexcept
		: impl_{std::move(impl)}
	{
	}

	friend class certificate;
};

/// Input iterator over distinguished name entries.
class distinguished_name::const_iterator
{
public:

	using iterator_concept = std::input_iterator_tag;
	using iterator_category = std::input_iterator_tag;
	using value_type = entry;
	using pointer = const entry *;
	using reference = const entry &;
	using difference_type = std::ptrdiff_t;

	const_iterator () = default;

	bool operator== (const const_iterator &that) const noexcept
	{
		return owner_ == that.owner_ && at_ == that.at_;
	}

	bool operator== (std::default_sentinel_t) const noexcept
	{
		return owner_ == nullptr;
	}

	const_iterator &operator++ () noexcept
	{
		load_next_entry();
		return *this;
	}

	reference operator* () const noexcept
	{
		return entry_;
	}

	pointer operator->() const noexcept
	{
		return &entry_;
	}

private:

	impl_ptr owner_;
	int at_{};
	oid_buffer oid_{};
	value_buffer value_{};
	entry entry_;

	explicit const_iterator (impl_ptr owner) noexcept
		: owner_{std::move(owner)}
	{
		load_next_entry();
	}

	void load_next_entry () noexcept;

	friend class distinguished_name;
};

inline distinguished_name::const_iterator distinguished_name::begin () const noexcept
{
	return const_iterator{impl_};
}

inline std::default_sentinel_t distinguished_name::end () noexcept
{
	return {};
}

} // namespace pal::crypto

template <>
struct std::formatter<pal::crypto::distinguished_name>
{
	template <typename ParseContext>
	static constexpr ParseContext::iterator parse (ParseContext &ctx)
	{
		auto it = ctx.begin();
		if (it != ctx.end() && *it != '}')
		{
			throw std::format_error("invalid format for pal::crypto::distinguished_name");
		}
		return it;
	}

	template <typename FormatContext>
	FormatContext::iterator format (const pal::crypto::distinguished_name &dn, FormatContext &ctx) const
	{
		static constexpr size_t max_string_size = 512;
		std::array<char, max_string_size> text{};
		auto [end, _] = dn.to_chars(text.data(), text.data() + text.size());
		return std::copy(text.data(), end, ctx.out());
	}
};
