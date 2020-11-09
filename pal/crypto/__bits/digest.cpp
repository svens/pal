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
	void hash_context<Ctx>::update (const void *data, size_t size) noexcept \
	{ \
		::Fn ## _Update(this, data, size); \
	} \
	\
	template <> \
	void hash_context<Ctx>::finish (void *digest, size_t) noexcept \
	{ \
		::Fn ## _Final(static_cast<uint8_t *>(digest), this); \
		::Fn ## _Init(this); \
	}

HASH_IMPL(::MD5_CTX, MD5)
HASH_IMPL(::SHA_CTX, SHA1)
HASH_IMPL(::SHA256_CTX, SHA256)
HASH_IMPL(__SHA384_CTX, SHA384)
HASH_IMPL(::SHA512_CTX, SHA512)

#undef HASH_IMPL


namespace {

inline HMAC_CTX *alloc () noexcept
{
	#if OPENSSL_VERSION_NUMBER < 0x10100000
		if (auto ctx = new HMAC_CTX)
		{
			::HMAC_CTX_init(ctx);
			return ctx;
		}
		return nullptr;
	#else
		return ::HMAC_CTX_new();
	#endif
}

} // namespace


void hmac_context_base::release (HMAC_CTX *ctx) noexcept
{
	#if OPENSSL_VERSION_NUMBER < 0x10100000
		::HMAC_CTX_cleanup(ctx);
		delete ctx;
	#else
		::HMAC_CTX_free(ctx);
	#endif
}


hmac_context_base::hmac_context_base (const EVP_MD *evp, const void *key, size_t size)
	: ctx{alloc(), release}
{
	if (!ctx)
	{
		throw std::bad_alloc();
	}
	if (!key)
	{
		key = "";
		size = 0U;
	}
	::HMAC_Init_ex(ctx.get(), key, size, evp, nullptr);
}


hmac_context_base::hmac_context_base (const hmac_context_base &that)
	: ctx{alloc(), release}
{
	if (!ctx)
	{
		throw std::bad_alloc();
	}
	::HMAC_CTX_copy(ctx.get(), that.ctx.get());
}


void hmac_context_base::update (const void *data, size_t size) noexcept
{
	::HMAC_Update(ctx.get(), static_cast<const uint8_t *>(data), size);
}


void hmac_context_base::finish (void *digest, size_t) noexcept
{
	::HMAC_Final(ctx.get(), static_cast<uint8_t *>(digest), nullptr);
	::HMAC_Init_ex(ctx.get(), nullptr, 0U, nullptr, nullptr);
}


#elif __pal_os_macos //{{{1


#define HASH_IMPL(Ctx, Fn) \
	template <> \
	hash_context<Ctx>::hash_context () noexcept \
	{ \
		::CC_ ## Fn ## _Init(this); \
	} \
	\
	template <> \
	void hash_context<Ctx>::update (const void *data, size_t size) noexcept \
	{ \
		::CC_ ## Fn ## _Update(this, data, size); \
	} \
	\
	template <> \
	void hash_context<Ctx>::finish (void *digest, size_t) noexcept \
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

#undef HASH_IMPL


#define HMAC_IMPL(Algorithm) \
	template <> \
	hmac_context<::kCCHmacAlg ## Algorithm>::hmac_context (const void *key, size_t size) noexcept \
		: init_ctx{} \
		, ctx{} \
	{ \
		::CCHmacInit(&init_ctx, ::kCCHmacAlg ## Algorithm, key, size); \
		ctx = init_ctx; \
	} \
	\
	template <> \
	void hmac_context<::kCCHmacAlg ## Algorithm>::update (const void *data, size_t size) noexcept \
	{ \
		::CCHmacUpdate(&ctx, data, size); \
	} \
	\
	template <> \
	void hmac_context<::kCCHmacAlg ## Algorithm>::finish (void *digest, size_t) noexcept \
	{ \
		::CCHmacFinal(&ctx, digest); \
		ctx = init_ctx; \
	}

HMAC_IMPL(MD5)
HMAC_IMPL(SHA1)
HMAC_IMPL(SHA256)
HMAC_IMPL(SHA384)
HMAC_IMPL(SHA512)

#undef HMAC_IMPL


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
BCRYPT_HASH_HANDLE make_context (const void *key, size_t size)
{
	BCRYPT_HASH_HANDLE handle;
	::BCryptCreateHash(
		provider<Algorithm, IsHMAC>(),
		&handle,
		nullptr,
		0,
		static_cast<PUCHAR>(const_cast<void *>(key)),
		static_cast<ULONG>(size),
		BCRYPT_HASH_REUSABLE_FLAG
	);
	return handle;
}


} // namespace


template <typename Algorithm, bool IsHMAC>
context_type<Algorithm, IsHMAC>::context_type (const void *key, size_t size)
	: handle{make_context<Algorithm, IsHMAC>(key, size)}
{ }


template <typename Algorithm, bool IsHMAC>
context_type<Algorithm, IsHMAC>::~context_type () noexcept
{
	if (handle)
	{
		::BCryptDestroyHash(handle);
	}
}


template <typename Algorithm, bool IsHMAC>
context_type<Algorithm, IsHMAC>::context_type (const context_type &that) noexcept
{
	::BCryptDuplicateHash(
		that.handle,
		&handle,
		nullptr,
		0,
		0
	);
}


template <typename Algorithm, bool IsHMAC>
void context_type<Algorithm, IsHMAC>::update (const void *data, size_t size) noexcept
{
	::BCryptHashData(
		handle,
		static_cast<PUCHAR>(const_cast<void *>(data)),
		static_cast<ULONG>(size),
		0
	);
}


template <typename Algorithm, bool IsHMAC>
void context_type<Algorithm, IsHMAC>::finish (void *digest, size_t size) noexcept
{
	::BCryptFinishHash(
		handle,
		static_cast<PUCHAR>(digest),
		static_cast<ULONG>(size),
		0
	);
}


// hash
template struct context_type<md5_algorithm, false>;
template struct context_type<sha1_algorithm, false>;
template struct context_type<sha256_algorithm, false>;
template struct context_type<sha384_algorithm, false>;
template struct context_type<sha512_algorithm, false>;

// hmac
template struct context_type<md5_algorithm, true>;
template struct context_type<sha1_algorithm, true>;
template struct context_type<sha256_algorithm, true>;
template struct context_type<sha384_algorithm, true>;
template struct context_type<sha512_algorithm, true>;


#endif //}}}1


} // namespace crypto::__bits


__pal_end
