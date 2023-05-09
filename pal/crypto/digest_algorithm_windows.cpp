#include <pal/__platform_sdk>
#include <pal/version>

#if __pal_os_windows

#include <pal/crypto/digest_algorithm>
#include <system_error>
#include <bcrypt.h>

namespace pal::crypto::algorithm {

namespace {

template <typename Algorithm> constexpr LPCWSTR algorithm_id = nullptr;

struct algorithm_provider
{
	BCRYPT_ALG_HANDLE handle{};
	std::error_code init_error{};

	algorithm_provider (LPCWSTR id, DWORD flags) noexcept
	{
		flags |= BCRYPT_HASH_REUSABLE_FLAG;
		auto r = ::BCryptOpenAlgorithmProvider(&handle, id, nullptr, flags);
		if (!NT_SUCCESS(r))
		{
			init_error.assign(::RtlNtStatusToDosError(r), std::system_category());
		}
	}

	~algorithm_provider () noexcept
	{
		if (handle)
		{
			::BCryptCloseAlgorithmProvider(handle, 0);
		}
	}
};

template <typename Algorithm, bool IsHMAC>
BCRYPT_HASH_HANDLE make_context (const void *key, size_t size, std::error_code &error) noexcept
{
	static algorithm_provider provider
	{
		algorithm_id<Algorithm>,
		IsHMAC ? BCRYPT_ALG_HANDLE_HMAC_FLAG : 0u
	};

	BCRYPT_HASH_HANDLE handle{};
	if (error = provider.init_error;  !error)
	{
		auto r = ::BCryptCreateHash(
			provider.handle,
			&handle,
			nullptr,
			0,
			static_cast<PUCHAR>(const_cast<void *>(key)),
			static_cast<ULONG>(size),
			BCRYPT_HASH_REUSABLE_FLAG
		);
		if (!NT_SUCCESS(r))
		{
			error.assign(::RtlNtStatusToDosError(r), std::system_category());
		}
	}
	return handle;
}

struct impl_base
{
	BCRYPT_HASH_HANDLE handle{};

	~impl_base () noexcept
	{
		if (handle)
		{
			::BCryptDestroyHash(handle);
		}
	}
};

} // namespace

#define __pal_crypto_digest_algorithm_impl(Algorithm, Context, Size) \
	namespace { template <> constexpr LPCWSTR algorithm_id<Algorithm> = BCRYPT_ ## Context ## _ALGORITHM; } \
	\
	struct Algorithm::hash::impl_type: impl_base { }; \
	\
	Algorithm::hash::~hash () noexcept = default; \
	Algorithm::hash::hash (hash &&) noexcept = default; \
	Algorithm::hash &Algorithm::hash::operator= (hash &&) noexcept = default; \
	\
	Algorithm::hash::hash (std::error_code &error) noexcept \
		: impl{new(std::nothrow) impl_type} \
	{ \
		if (!impl) \
		{ \
			error = std::make_error_code(std::errc::not_enough_memory); \
		} \
		else if (impl->handle = make_context<Algorithm, false>(nullptr, 0, error);  error) \
		{ \
			impl.reset(); \
		} \
	} \
	\
	void Algorithm::hash::update (std::span<const std::byte> input) noexcept \
	{ \
		::BCryptHashData( \
			impl->handle, \
			reinterpret_cast<PUCHAR>(const_cast<std::byte *>(input.data())), \
			static_cast<ULONG>(input.size()), \
			0 \
		); \
	} \
	\
	void Algorithm::hash::finish (std::span<std::byte, digest_size> digest) noexcept \
	{ \
		::BCryptFinishHash( \
			impl->handle, \
			reinterpret_cast<PUCHAR>(digest.data()), \
			static_cast<ULONG>(digest_size), \
			0 \
		); \
	}

__pal_crypto_digest_algorithm(__pal_crypto_digest_algorithm_impl)

} // namespace pal::crypto::algorithm

#endif // __pal_os_windows
