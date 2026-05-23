#include <pal/crypto/__crypto.hpp>

#if __pal_crypto_windows

// clang-format off
#include <pal/crypto/digest_algorithm.hpp>
#include <bcrypt.h>
#include <memory>
#include <tuple>
// clang-format on

namespace pal::crypto::algorithm
{

namespace
{

template <typename T>
constexpr LPCWSTR algorithm_id = nullptr;

struct algorithm_provider
{
	BCRYPT_ALG_HANDLE handle{};
	std::error_code init_error{};

	algorithm_provider (LPCWSTR id, DWORD flags) noexcept
	{
		flags |= BCRYPT_HASH_REUSABLE_FLAG;
		const auto r = ::BCryptOpenAlgorithmProvider(&handle, id, nullptr, flags);
		if (!NT_SUCCESS(r))
		{
			init_error.assign(::RtlNtStatusToDosError(r), std::system_category());
		}
	}

	~algorithm_provider () noexcept
	{
		if (handle != nullptr)
		{
			::BCryptCloseAlgorithmProvider(handle, 0);
		}
	}
};

template <typename Algorithm, bool IsHMAC>
BCRYPT_HASH_HANDLE make_context (const void *key, size_t key_size, std::error_code &ec) noexcept
{
	static algorithm_provider provider{algorithm_id<Algorithm>, IsHMAC ? BCRYPT_ALG_HANDLE_HMAC_FLAG : 0u};

	static constexpr unsigned char empty_key{};
	if constexpr (IsHMAC)
	{
		if (key == nullptr)
		{
			key = &empty_key;
		}
	}

	BCRYPT_HASH_HANDLE handle{};
	if (ec = provider.init_error; !ec)
	{
		const auto r = ::BCryptCreateHash(
			provider.handle,
			&handle,
			nullptr,
			0,
			static_cast<PUCHAR>(const_cast<void *>(key)),
			static_cast<ULONG>(key_size),
			BCRYPT_HASH_REUSABLE_FLAG
		);
		if (!NT_SUCCESS(r))
		{
			ec.assign(::RtlNtStatusToDosError(r), std::system_category());
		}
	}
	return handle;
}

struct impl_base
{
	BCRYPT_HASH_HANDLE handle{};

	~impl_base () noexcept
	{
		if (handle != nullptr)
		{
			::BCryptDestroyHash(handle);
		}
	}
};

} // namespace

// clang-format off

//
// Hash
//

#define __pal_crypto_impl_hash(Tag, Name, Size) \
	namespace { template <> constexpr LPCWSTR algorithm_id<Tag> = BCRYPT_##Name##_ALGORITHM; } \
	\
	struct Tag::hash::impl_type: impl_base {}; \
	\
	Tag::hash::~hash () noexcept = default; \
	Tag::hash::hash (hash &&) noexcept = default; \
	Tag::hash &Tag::hash::operator= (hash &&) noexcept = default; \
	\
	Tag::hash::hash (std::error_code &ec) noexcept \
		: impl{new(std::nothrow) impl_type} \
	{ \
		if (impl) \
		{ \
			impl->handle = make_context<Tag, false>(nullptr, 0, ec); \
		} \
		else \
		{ \
			ec = std::make_error_code(std::errc::not_enough_memory); \
		} \
	} \
	\
	void Tag::hash::update (std::span<const std::byte> input) noexcept \
	{ \
		std::ignore = ::BCryptHashData( \
			impl->handle, \
			reinterpret_cast<PUCHAR>(const_cast<std::byte *>(input.data())), \
			static_cast<ULONG>(input.size()), \
			0 \
		); \
	} \
	\
	void Tag::hash::finish (std::span<std::byte, Size> digest) noexcept \
	{ \
		std::ignore = ::BCryptFinishHash( \
			impl->handle, \
			reinterpret_cast<PUCHAR>(digest.data()), \
			static_cast<ULONG>(Size), \
			0 \
		); \
	}

__pal_crypto_digest_algorithm(__pal_crypto_impl_hash)
#undef __pal_crypto_impl_hash

//
// HMAC
//

#define __pal_crypto_impl_hmac(Tag, Name, Size) \
	struct Tag::hmac::impl_type: impl_base {}; \
	\
	Tag::hmac::~hmac () noexcept = default; \
	Tag::hmac::hmac (hmac &&) noexcept = default; \
	Tag::hmac &Tag::hmac::operator= (hmac &&) noexcept = default; \
	\
	Tag::hmac::hmac (std::span<const std::byte> key, std::error_code &ec) noexcept \
		: impl{new(std::nothrow) impl_type} \
	{ \
		if (impl) \
		{ \
			impl->handle = make_context<Tag, true>(key.data(), key.size(), ec); \
		} \
		else \
		{ \
			ec = std::make_error_code(std::errc::not_enough_memory); \
		} \
	} \
	\
	void Tag::hmac::update (std::span<const std::byte> input) noexcept \
	{ \
		std::ignore = ::BCryptHashData( \
			impl->handle, \
			reinterpret_cast<PUCHAR>(const_cast<std::byte *>(input.data())), \
			static_cast<ULONG>(input.size()), \
			0 \
		); \
	} \
	\
	void Tag::hmac::finish (std::span<std::byte, Size> digest) noexcept \
	{ \
		std::ignore = ::BCryptFinishHash( \
			impl->handle, \
			reinterpret_cast<PUCHAR>(digest.data()), \
			static_cast<ULONG>(Size), \
			0 \
		); \
	}

__pal_crypto_digest_algorithm(__pal_crypto_impl_hmac)
#undef __pal_crypto_impl_hmac

// clang-format off

} // namespace pal::crypto::algorithm

#endif // __pal_crypto_windows
