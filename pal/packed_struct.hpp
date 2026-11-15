#pragma once

/**
 * \file pal/packed_struct.hpp
 * Packed structures viewed over byte buffers
 */

#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <memory>
#include <span>
#include <type_traits>

namespace pal
{

namespace __packed_struct
{

template <typename T>
constexpr bool is_implicit_lifetime_v = std::is_scalar_v<T>
	|| std::is_array_v<T>
	|| std::is_aggregate_v<T>
	|| std::is_trivially_default_constructible_v<T>
	|| std::is_trivially_copy_constructible_v<T>
	|| std::is_trivially_move_constructible_v<T>
;

template <typename T>
struct integer_of
{
	using type = T;
};

template <typename T>
	requires std::is_enum_v<T>
struct integer_of<T>: std::underlying_type<T>
{
};

template <typename T>
using integer_of_t = typename integer_of<T>::type;

template <typename T>
concept fixed_enum = std::is_enum_v<T> && requires
{
	T{0};
};

template <typename T>
concept endian_value = std::same_as<T, std::remove_cv_t<T>>
	&& (std::integral<T> || fixed_enum<T>)
	&& !std::same_as<integer_of_t<T>, bool>
;

} // namespace __packed_struct

/// Requirements for type whose object representation is its layout in byte buffer: cv-unqualified, trivially
/// copyable, implicit-lifetime, alignment 1 and no padding. Satisfied by \c std::byte, byte arrays,
/// basic_endian and structures composed of them.
///
/// Not checkable, left to the author of \a T:
/// - every byte pattern must be valid value of \a T: no \c bool members
/// - no bit-field members: their layout is implementation-defined
/// - no \c const members
/// - members wider than byte must be basic_endian: structure of native integers packed with compiler
///   extension satisfies the concept but its values are in host byte order
template <typename T>
concept packed_struct = std::same_as<T, std::remove_cv_t<T>>
	&& __packed_struct::is_implicit_lifetime_v<T>
	&& alignof(T) == 1
	&& std::has_unique_object_representations_v<T>
;

/// Integer or enumeration with fixed underlying type \a T stored as \c sizeof(T) bytes in byte order
/// \a Order (big or little). Alignment is 1, making it usable as packed_struct member at any offset.
template <__packed_struct::endian_value T, std::endian Order>
	requires(Order == std::endian::big || Order == std::endian::little)
class basic_endian
{
public:

	/// Stored value type
	using value_type = T;

	/// Construct with zero value
	constexpr basic_endian () noexcept = default;

	/// Construct with \a value
	constexpr basic_endian (T value) noexcept
		: bytes_{std::bit_cast<bytes_type>(reorder(static_cast<integer_type>(value)))}
	{
	}

	/// Return stored value
	[[nodiscard]] constexpr T value () const noexcept
	{
		return static_cast<T>(reorder(std::bit_cast<integer_type>(bytes_)));
	}

	/// Return stored value
	constexpr operator T () const noexcept
	{
		return value();
	}

private:

	using integer_type = __packed_struct::integer_of_t<T>;
	using bytes_type = std::array<std::byte, sizeof(T)>;

	bytes_type bytes_{};

	static constexpr integer_type reorder (integer_type value) noexcept
	{
		if constexpr (Order != std::endian::native)
		{
			return std::byteswap(value);
		}
		else
		{
			return value;
		}
	}
};

/// \a T stored in big-endian (network) byte order
template <typename T>
using big_endian = basic_endian<T, std::endian::big>;

/// \a T stored in little-endian byte order
template <typename T>
using little_endian = basic_endian<T, std::endian::little>;

/// View \a bytes as \a T. Returned reference is valid while storage of \a bytes is and until overlapping
/// bytes are viewed with different type or offset; writes through it modify \a bytes.
template <packed_struct T>
[[nodiscard]] T &packed_as (std::span<std::byte, sizeof(T)> bytes) noexcept
{
	#ifdef __cpp_lib_start_lifetime_as
	{
		return *std::start_lifetime_as<T>(bytes.data());
	}
	#else
	{
		// TODO(svens): fallback until every stdlib ships start_lifetime_as
		return *reinterpret_cast<T *>(bytes.data());
	}
	#endif
}

/// View \a bytes as \a T. Returned reference is valid while storage of \a bytes is and until overlapping
/// bytes are viewed with different type or offset.
template <packed_struct T>
[[nodiscard]] const T &packed_as (std::span<const std::byte, sizeof(T)> bytes) noexcept
{
	#ifdef __cpp_lib_start_lifetime_as
	{
		return *std::start_lifetime_as<T>(bytes.data());
	}
	#else
	{
		// TODO(svens): fallback until every stdlib ships start_lifetime_as
		return *reinterpret_cast<const T *>(bytes.data());
	}
	#endif
}

} // namespace pal
