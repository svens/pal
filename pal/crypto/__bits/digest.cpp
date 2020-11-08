#include <pal/__bits/platform_sdk>
#include <pal/crypto/__bits/digest>
#include <pal/expect>
#include <system_error>


__pal_begin


namespace crypto::__bits {


#if __pal_os_linux //{{{1


#define HASH_IMPL(Ctx, Fn) \
	template <> \
	hash_context<Ctx>::hash_context () noexcept \
	{ \
		::Fn ## _Init(this); \
	} \
	\
	template <> \
	void hash_context<Ctx>::update ( \
		const void *data, size_t size) noexcept \
	{ \
		::Fn ## _Update(this, data, size); \
	} \
	\
	template <> \
	void hash_context<Ctx>::finish ( \
		void *digest, size_t) noexcept \
	{ \
		::Fn ## _Final(static_cast<uint8_t *>(digest), this); \
		::Fn ## _Init(this); \
	}

HASH_IMPL(::MD5_CTX, MD5)
HASH_IMPL(::SHA_CTX, SHA1)
HASH_IMPL(::SHA256_CTX, SHA256)
HASH_IMPL(__SHA384_CTX, SHA384)
HASH_IMPL(::SHA512_CTX, SHA512)


#elif __pal_os_macos //{{{1


#define HASH_IMPL(Ctx, Fn) \
	template <> \
	hash_context<Ctx>::hash_context () noexcept \
	{ \
		::CC_ ## Fn ## _Init(this); \
	} \
	\
	template <> \
	void hash_context<Ctx>::update ( \
		const void *data, size_t size) noexcept \
	{ \
		::CC_ ## Fn ## _Update(this, data, size); \
	} \
	\
	template <> \
	void hash_context<Ctx>::finish ( \
		void *digest, size_t) noexcept \
	{ \
		::CC_ ## Fn ## _Final(static_cast<uint8_t *>(digest), this); \
		::CC_ ## Fn ## _Init(this); \
	}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
HASH_IMPL(::CC_MD5_CTX, MD5)
#pragma GCC diagnostic pop

HASH_IMPL(::CC_SHA1_CTX, SHA1)
HASH_IMPL(::CC_SHA256_CTX, SHA256)
HASH_IMPL(__CC_SHA384_CTX, SHA384)
HASH_IMPL(::CC_SHA512_CTX, SHA512)


#elif __pal_os_windows //{{{1


namespace {


inline void check_result (NTSTATUS result, const char *func)
{
	if (!NT_SUCCESS(result))
	{
		std::error_code error(
			::RtlNtStatusToDosError(result),
			std::system_category()
		);
		throw std::system_error(error, func);
	}
}
#define call(func, ...) check_result(func(__VA_ARGS__), #func)


struct provider_handle
{
	BCRYPT_ALG_HANDLE handle;

	provider_handle (LPCWSTR id, DWORD flags, size_t expected_digest_size)
	{
		flags |= BCRYPT_HASH_REUSABLE_FLAG;
		call(::BCryptOpenAlgorithmProvider, &handle, id, nullptr, flags);

		DWORD digest_size, copied;
		call(::BCryptGetProperty,
			handle,
			BCRYPT_HASH_LENGTH,
			reinterpret_cast<PBYTE>(&digest_size),
			sizeof(digest_size),
			&copied,
			0
		);
		pal_expect(digest_size == expected_digest_size,
			"Unexpected digest size"
		);
	}

	~provider_handle () noexcept
	{
		::BCryptCloseAlgorithmProvider(handle, 0);
	}
};


template <typename Algorithm, bool IsHMAC>
BCRYPT_ALG_HANDLE provider ()
{
	static provider_handle provider_
	{
		Algorithm::id,
		IsHMAC ? BCRYPT_ALG_HANDLE_HMAC_FLAG : 0U,
		digest_size_v<Algorithm>
	};
	return provider_.handle;
}


template <typename Algorithm, bool IsHMAC>
BCRYPT_HASH_HANDLE make_context (
	const void *secret = nullptr,
	size_t secret_size = 0)
{
	BCRYPT_HASH_HANDLE handle;
	::BCryptCreateHash(
		provider<Algorithm, IsHMAC>(),
		&handle,
		nullptr,
		0,
		static_cast<PUCHAR>(const_cast<void *>(secret)),
		static_cast<ULONG>(secret_size),
		BCRYPT_HASH_REUSABLE_FLAG
	);
	return handle;
}


} // namespace


template <typename Impl>
context_type<Impl>::context_type ()
	: handle{make_context<Impl, false>()}
{ }


template <typename Impl>
context_type<Impl>::~context_type () noexcept
{
	if (handle)
	{
		::BCryptDestroyHash(handle);
	}
}


template <typename Impl>
context_type<Impl>::context_type (const context_type &that) noexcept
{
	::BCryptDuplicateHash(
		that.handle,
		&handle,
		nullptr,
		0,
		0
	);
}


template <typename Impl>
void context_type<Impl>::update (const void *data, size_t size) noexcept
{
	::BCryptHashData(
		handle,
		static_cast<PUCHAR>(const_cast<void *>(data)),
		static_cast<ULONG>(size),
		0
	);
}


template <typename Impl>
void context_type<Impl>::finish (void *digest, size_t size) noexcept
{
	::BCryptFinishHash(
		handle,
		static_cast<PUCHAR>(digest),
		static_cast<ULONG>(size),
		0
	);
}


template struct context_type<md5_algorithm>;
template struct context_type<sha1_algorithm>;
template struct context_type<sha256_algorithm>;
template struct context_type<sha384_algorithm>;
template struct context_type<sha512_algorithm>;


#endif //}}}1


} // namespace crypto::__bits


__pal_end
