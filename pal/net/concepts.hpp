#pragma once

/**
 * \file pal/net/concepts.hpp
 * Network type concepts
 */

#include <pal/result.hpp>
#include <concepts>
#include <cstddef>

namespace pal::net
{

// clang-format off
// Requires expression braces: Allman style not applied (see .clang-format TODO #7)

/// Requirements for types used as network protocol template parameters
template <typename T>
concept protocol = requires(const T &p)
{
	requires std::constructible_from<T, int>;
	{ p.family() } -> std::same_as<int>;
};

/// Requirements for types used as network endpoint template parameters
template <typename T>
concept endpoint = requires(T ep, const T cep, size_t n)
{
	requires std::default_initializable<T>;
	requires std::copyable<T>;
	requires std::equality_comparable<T>;
	typename T::protocol_type;
	requires protocol<typename T::protocol_type>;
	{ cep.protocol() } -> std::same_as<typename T::protocol_type>;
	{ ep.data() } -> std::same_as<void *>;
	{ cep.data() } -> std::same_as<const void *>;
	{ cep.size() } -> std::same_as<size_t>;
	{ cep.capacity() } -> std::same_as<size_t>;
	{ ep.resize(n) };
};

/// Requirements for types used as gettable socket option template parameters
template <typename T, typename Protocol>
concept gettable_socket_option = requires(T &opt, const Protocol &p, size_t n)
{
	{ opt.level(p) } -> std::convertible_to<int>;
	{ opt.name(p) } -> std::convertible_to<int>;
	{ opt.data(p) } -> std::convertible_to<void *>;
	{ opt.size(p) } -> std::same_as<size_t>;
	{ opt.resize(p, n) } -> std::same_as<result<void>>;
};

/// Requirements for types used as settable socket option template parameters
template <typename T, typename Protocol>
concept settable_socket_option = requires(const T &opt, const Protocol &p)
{
	{ opt.level(p) } -> std::convertible_to<int>;
	{ opt.name(p) } -> std::convertible_to<int>;
	{ opt.data(p) } -> std::convertible_to<const void *>;
	{ opt.size(p) } -> std::same_as<size_t>;
};

// clang-format on

} // namespace pal::net
