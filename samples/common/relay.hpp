#pragma once

#include <cstring>
#include <span>
#include <type_traits>

namespace relay
{

inline constexpr size_t max_datagram_size = 1'200;

#pragma pack(push, 1)

struct header
{
	uint64_t session;
	uint32_t seq;
};
static_assert(sizeof(header) == 12);

struct registration
{
	uint64_t session;
	uint16_t client_port;
};
static_assert(sizeof(registration) == 10);

struct allocation
{
	uint64_t session;
	uint16_t port;
};
static_assert(sizeof(allocation) == 10);

#pragma pack(pop)

template <typename T>
	requires std::is_trivially_copyable_v<T>
std::span<const std::byte> as_span (const T &value) noexcept
{
	return {reinterpret_cast<const std::byte *>(&value), sizeof(T)};
}

template <typename T>
	requires std::is_trivially_copyable_v<T>
T read (std::span<const std::byte> datagram) noexcept
{
	T value;
	std::memcpy(&value, datagram.data(), sizeof(T));
	return value;
}

inline uint64_t session (std::span<const std::byte> datagram) noexcept
{
	return read<uint64_t>(datagram);
}

} // namespace relay
