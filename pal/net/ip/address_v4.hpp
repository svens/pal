#pragma once

/**
 * \file pal/net/ip/address_v4.hpp
 * IPv4 address
 */

#include <pal/hash.hpp>
#include <pal/result.hpp>
#include <array>
#include <charconv>
#include <compare>
#include <format>
#include <string_view>

namespace pal::net::ip
{

namespace __address_v4
{

[[nodiscard]] char *ntop (const uint8_t *bytes, char *first, char *last) noexcept;

} // namespace __address_v4

// NOLINTBEGIN(readability-magic-numbers)

/// IPv4 address
class address_v4
{
public:

	/// Maximum human readable representation length
	static constexpr size_t max_string_length = sizeof("255.255.255.255") - 1;

	/// Integer representation of IPv4 address
	using uint_type = uint32_t;

	/// Binary representation of IPv4 address
	struct bytes_type: std::array<uint8_t, 4>
	{
		bytes_type () noexcept = default;

		/// Construct address from exactly four byte values
		template <typename... T>
			requires(sizeof...(T) == 4)
		explicit constexpr bytes_type(T... v) noexcept
			: std::array<uint8_t, 4>{{static_cast<uint8_t>(v)...}}
		{
		}
	};

	/// Construct new unspecified address
	constexpr address_v4 () noexcept
		: bytes_{0, 0, 0, 0}
	{
	}

	/// Construct new address from \a value (host byte order)
	explicit constexpr address_v4 (uint_type value) noexcept
		: bytes_{to_bytes(value)}
	{
	}

	/// Construct new address from \a bytes
	constexpr address_v4 (const bytes_type &bytes) noexcept
		: bytes_{bytes}
	{
	}

	constexpr address_v4 (const address_v4 &) noexcept = default;
	address_v4 &operator= (const address_v4 &) noexcept = default;

	/// Return integer representation of \a this address (in host byte order)
	[[nodiscard]] constexpr uint_type to_uint () const noexcept
	{
		return to_uint(bytes_);
	}

	/// Return binary representation of \a this address
	[[nodiscard]] constexpr const bytes_type &to_bytes () const noexcept
	{
		return bytes_;
	}

	/**
	 * Convert \a this address to human readable textual representation by
	 * filling range [\a first, \a last).
	 *
	 * On success, returns std::to_chars_result with ptr pointing past
	 * last filled character and ec default value initialized.
	 *
	 * On failure, ptr is set to \a last and ec std::errc::value_too_large.
	 */
	[[nodiscard]] std::to_chars_result to_chars (char *first, char *last) const noexcept
	{
		if (auto *p = __address_v4::ntop(bytes_.data(), first, last))
		{
			return {.ptr = p, .ec = std::errc{}};
		}
		return {.ptr = last, .ec = std::errc::value_too_large};
	}

	/**
	 * Parse human readable IPv4 address from range [\a first, \a last).
	 *
	 * On success, returns std::from_chars_result with ptr pointing to
	 * first character not matching the IPv4 address text.
	 *
	 * On failure, ptr is set to \a first and ec std::errc::invalid_argument.
	 */
	[[nodiscard]] std::from_chars_result from_chars (const char *first, const char *last) noexcept;

	/// Return true if \a this is unspecified address (0.0.0.0)
	[[nodiscard]] constexpr bool is_unspecified () const noexcept
	{
		return bytes_[0] == 0 && bytes_[1] == 0 && bytes_[2] == 0 && bytes_[3] == 0;
	}

	/// Return true if \a this is loopback address (127.0.0.0 - 127.255.255.255)
	[[nodiscard]] constexpr bool is_loopback () const noexcept
	{
		return bytes_[0] == 0x7f;
	}

	/// Return true if \a this is private address (RFC1918)
	[[nodiscard]] constexpr bool is_private () const noexcept
	{
		return
			// 10.0.0.0 - 10.255.255.255
			(bytes_[0] == 0x0a) ||

			// 172.16.0.0 - 172.31.255.255
			(bytes_[0] == 0xac && bytes_[1] >= 0x10 && bytes_[1] <= 0x1f) ||

			// 192.168.0.0 - 192.168.255.255
			(bytes_[0] == 0xc0 && bytes_[1] == 0xa8);
	}

	constexpr auto operator<=> (const address_v4 &) const noexcept = default;

	/// Return hash value for \a this
	[[nodiscard]] constexpr uint64_t hash () const noexcept
	{
		return fnv_1a_64(bytes_);
	}

	/// Unspecified IPv4 address
	static const address_v4 any;

	/// IPv4 loopback address
	static const address_v4 loopback;

	/// IPv4 broadcast address
	static const address_v4 broadcast;

private:

	bytes_type bytes_;

	static constexpr bytes_type to_bytes (uint_type value) noexcept
	{
		return bytes_type{
			static_cast<uint8_t>((value >> 24) & 0xff),
			static_cast<uint8_t>((value >> 16) & 0xff),
			static_cast<uint8_t>((value >> 8) & 0xff),
			static_cast<uint8_t>(value & 0xff),
		};
	}

	static constexpr uint_type to_uint (const bytes_type &bytes) noexcept
	{
		const auto b0 = static_cast<uint_type>(bytes[0]) << 24;
		const auto b1 = static_cast<uint_type>(bytes[1]) << 16;
		const auto b2 = static_cast<uint_type>(bytes[2]) << 8;
		return b0 | b1 | b2 | static_cast<uint_type>(bytes[3]);
	}
};

// NOLINTEND(readability-magic-numbers)

inline constexpr address_v4 address_v4::any{address_v4::bytes_type{0, 0, 0, 0}};
inline constexpr address_v4 address_v4::loopback{address_v4::bytes_type{127, 0, 0, 1}};
inline constexpr address_v4 address_v4::broadcast{address_v4::bytes_type{255, 255, 255, 255}};

/// Return new address from \a value
[[nodiscard]] constexpr address_v4 make_address_v4 (const address_v4::bytes_type &value) noexcept
{
	return address_v4{value};
}

/// \copydoc make_address_v4()
[[nodiscard]] constexpr address_v4 make_address_v4 (address_v4::uint_type value) noexcept
{
	return address_v4{value};
}

/// Return new address parsed from human readable \a text
[[nodiscard]] inline result<address_v4> make_address_v4 (std::string_view text) noexcept
{
	address_v4 addr;
	auto [_, ec] = addr.from_chars(text.data(), text.data() + text.size());
	if (ec == std::errc{})
	{
		return addr;
	}
	return make_unexpected(ec);
}

} // namespace pal::net::ip

namespace std
{

template <>
struct hash<pal::net::ip::address_v4>
{
	[[nodiscard]] size_t operator() (const pal::net::ip::address_v4 &a) const noexcept
	{
		return a.hash();
	}
};

template <>
struct formatter<pal::net::ip::address_v4>
{
	template <typename ParseContext>
	static constexpr ParseContext::iterator parse (ParseContext &ctx)
	{
		auto it = ctx.begin();
		if (it != ctx.end() && *it != '}')
		{
			throw std::format_error("invalid format for pal::net::ip::address_v4");
		}
		return it;
	}

	template <typename FormatContext>
	FormatContext::iterator format (const pal::net::ip::address_v4 &a, FormatContext &ctx) const
	{
		std::array<char, pal::net::ip::address_v4::max_string_length + 1> text{};
		auto [end, _] = a.to_chars(text.data(), text.data() + text.size());
		return std::copy(text.data(), end, ctx.out());
	}
};

} // namespace std
