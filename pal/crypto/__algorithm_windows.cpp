#include <pal/__platform_sdk>
#include <pal/version>

#if __pal_os_windows

#include <pal/crypto/__algorithm>
#include <system_error>
#include <bcrypt.h>

namespace pal::crypto::__algorithm {

namespace {

template <typename Algorithm> constexpr LPCWSTR algorithm_id = nullptr;
template <> constexpr LPCWSTR algorithm_id<md5_hash> = BCRYPT_MD5_ALGORITHM;

template <typename Algorithm> constexpr size_t digest_size = 0;
template <> constexpr size_t digest_size<md5_hash> = 16;

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
BCRYPT_HASH_HANDLE make_context (const void *key = nullptr, size_t size = 0) noexcept
{
	static algorithm_provider provider
	{
		algorithm_id<Algorithm>,
		IsHMAC ? BCRYPT_ALG_HANDLE_HMAC_FLAG : 0u
	};

	BCRYPT_HASH_HANDLE handle;
	::BCryptCreateHash(
		provider.handle,
		&handle,
		nullptr,
		0,
		static_cast<PUCHAR>(const_cast<void *>(key)),
		static_cast<ULONG>(size),
		BCRYPT_HASH_REUSABLE_FLAG
	);
	return handle;
}

struct impl_base
{
	BCRYPT_HASH_HANDLE handle;

	impl_base (BCRYPT_HASH_HANDLE handle)
		: handle{handle}
	{ }

	~impl_base () noexcept
	{
		if (handle)
		{
			::BCryptDestroyHash(handle);
		}
	}

	void update (const void *ptr, size_t size) noexcept
	{
		::BCryptHashData(
			handle,
			static_cast<PUCHAR>(const_cast<void *>(ptr)),
			static_cast<ULONG>(size),
			0
		);
	}

	void finish (void *digest, size_t size) noexcept
	{
		::BCryptFinishHash(
			handle,
			static_cast<PUCHAR>(digest),
			static_cast<ULONG>(size),
			0
		);
	}
};

} // namespace

#define __pal_crypto_algorithm_impl(Algorithm, unused) \
	struct Algorithm ## _hash::impl_type: impl_base \
	{ \
		impl_type () \
			: impl_base(make_context<Algorithm ## _hash, false>()) \
		{} \
	}; \
	\
	Algorithm ## _hash::~Algorithm ## _hash () noexcept = default; \
	\
	Algorithm ## _hash::Algorithm ## _hash () \
		: impl{std::make_unique<impl_type>()} \
	{ } \
	\
	void Algorithm ## _hash::update (const void *ptr, size_t size) noexcept \
	{ \
		impl->update(ptr, size); \
	} \
	\
	void Algorithm ## _hash::finish (void *digest) noexcept \
	{ \
		impl->finish(digest, digest_size<Algorithm ## _hash>); \
	}

__pal_crypto_algorithm(__pal_crypto_algorithm_impl);

#undef __pal_crypto_algorithm_impl

} // namespace pal::crypto::__algorithm

#endif // __pal_os_windows
