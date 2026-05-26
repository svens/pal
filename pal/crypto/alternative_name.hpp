#pragma once

/**
 * \file pal/crypto/alternative_name.hpp
 * X.509 certificate alternative name
 */

#include <array>
#include <charconv>
#include <format>
#include <iterator>
#include <memory>
#include <string_view>
#include <variant>

namespace pal::crypto
{

class certificate;

/// Base for alternative name entry value types. Label is the format prefix used in to_chars/formatter.
template <const std::string_view &Label>
struct alternative_name_entry_value: std::string_view
{
	using std::string_view::string_view;
	static constexpr std::string_view label = Label;
};

inline constexpr std::string_view dns_label = "dns";
/// DNS name alternative name entry.
using dns_name = alternative_name_entry_value<dns_label>;

inline constexpr std::string_view email_label = "email";
/// Email address alternative name entry.
using email_address = alternative_name_entry_value<email_label>;

inline constexpr std::string_view ip_label = "ip";
/// IP address alternative name entry (text form: dotted-decimal or colon-hex).
using ip_address = alternative_name_entry_value<ip_label>;

inline constexpr std::string_view uri_label = "uri";
/// URI alternative name entry.
using uri = alternative_name_entry_value<uri_label>;

/// Single entry in an alternative name extension.
using alternative_name_entry = std::variant<std::monostate, dns_name, email_address, ip_address, uri>;

/// Sequence of entries from a certificate's Subject or Issuer Alternative Name extension.
/// Absent extension or OOM during construction yields an empty range.
class alternative_name
{
public:

	class const_iterator;

	/// Returns iterator to the first entry.
	[[nodiscard]] const_iterator begin () const noexcept;

	/// Returns sentinel representing one-past-the-end.
	[[nodiscard]] static std::default_sentinel_t end () noexcept;

	/// Format alternative name as comma-separated type=value string into [first, last).
	[[nodiscard]] std::to_chars_result to_chars (char *first, char *last) const noexcept;

private:

	struct impl_type;
	using impl_ptr = std::shared_ptr<impl_type>;
	impl_ptr impl_;

	static constexpr size_t max_entry_size = 512;
	using entry_buffer = std::array<char, max_entry_size>;

	explicit alternative_name (impl_ptr impl) noexcept
		: impl_{std::move(impl)}
	{
	}

	friend class certificate;
};

/// Input iterator over alternative name entries.
///
/// The entry value (and any string_view pointing into it) is valid only until
/// the next call to operator++() or iterator destruction.
class alternative_name::const_iterator
{
public:

	using iterator_concept = std::input_iterator_tag;
	using iterator_category = std::input_iterator_tag;
	using value_type = alternative_name_entry;
	using pointer = const alternative_name_entry *;
	using reference = const alternative_name_entry &;
	using difference_type = std::ptrdiff_t;

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
	entry_buffer buffer_{};
	alternative_name_entry entry_;

	explicit const_iterator (impl_ptr owner) noexcept
		: owner_{std::move(owner)}
	{
		if (owner_)
		{
			load_next_entry();
		}
	}

	void load_next_entry () noexcept;

	friend class alternative_name;
};

inline alternative_name::const_iterator alternative_name::begin () const noexcept
{
	return const_iterator{impl_};
}

inline std::default_sentinel_t alternative_name::end () noexcept
{
	return {};
}

} // namespace pal::crypto

template <>
struct std::formatter<pal::crypto::alternative_name>
{
	template <typename ParseContext>
	static constexpr ParseContext::iterator parse (ParseContext &ctx)
	{
		auto it = ctx.begin();
		if (it != ctx.end() && *it != '}')
		{
			throw std::format_error("invalid format for pal::crypto::alternative_name");
		}
		return it;
	}

	template <typename FormatContext>
	FormatContext::iterator format (const pal::crypto::alternative_name &an, FormatContext &ctx) const
	{
		static constexpr size_t max_string_size = 512;
		std::array<char, max_string_size> text{};
		auto [end, _] = an.to_chars(text.data(), text.data() + text.size());
		return std::copy(text.data(), end, ctx.out());
	}
};
