#include <pal/__bits/platform_sdk>
#include <pal/crypto/hash>
#include <pal/expect>


__pal_begin


namespace crypto {


#if __pal_os_linux //{{{1


#define HASH_IMPL(Function, Type) \
	template <> \
	hash<Type>::hash () \
	{ \
		::Function ## _Init(&context_); \
	} \
	\
	template <> \
	void hash<Type>::update (const void *input, size_t size) noexcept \
	{ \
		::Function ## _Update(&context_, input, size); \
	} \
	\
	template <> \
	void hash<Type>::finish (void *result, size_t) noexcept \
	{ \
		::Function ## _Final(static_cast<uint8_t *>(result), &context_); \
		::Function ## _Init(&context_); \
	}

HASH_IMPL(MD5, md5)
HASH_IMPL(SHA1, sha1)
HASH_IMPL(SHA256, sha256)
HASH_IMPL(SHA384, sha384)
HASH_IMPL(SHA512, sha512)


#elif __pal_os_macos //{{{1


#define HASH_IMPL(Function, Type) \
	template <> \
	hash<Type>::hash () \
	{ \
		::CC_ ## Function ## _Init(&context_); \
	} \
	\
	template <> \
	void hash<Type>::update (const void *input, size_t size) noexcept \
	{ \
		::CC_ ## Function ## _Update(&context_, input, size); \
	} \
	\
	template <> \
	void hash<Type>::finish (void *result, size_t) noexcept \
	{ \
		::CC_ ## Function ## _Final(static_cast<uint8_t *>(result), &context_); \
		::CC_ ## Function ## _Init(&context_); \
	}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
HASH_IMPL(MD5, md5)
HASH_IMPL(SHA1, sha1)
HASH_IMPL(SHA256, sha256)
HASH_IMPL(SHA384, sha384)
HASH_IMPL(SHA512, sha512)
#pragma GCC diagnostic pop


#elif __pal_os_windows //{{{1


namespace __bits {


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


template <typename Algorithm> constexpr LPCWSTR algorithm_id_v = {};
template <> constexpr LPCWSTR algorithm_id_v<md5> = BCRYPT_MD5_ALGORITHM;
template <> constexpr LPCWSTR algorithm_id_v<sha1> = BCRYPT_SHA1_ALGORITHM;
template <> constexpr LPCWSTR algorithm_id_v<sha256> = BCRYPT_SHA256_ALGORITHM;
template <> constexpr LPCWSTR algorithm_id_v<sha384> = BCRYPT_SHA384_ALGORITHM;
template <> constexpr LPCWSTR algorithm_id_v<sha512> = BCRYPT_SHA512_ALGORITHM;


template <bool IsHMAC> constexpr DWORD algorithm_flags_v = 0U;
template <> constexpr DWORD algorithm_flags_v<true> = BCRYPT_ALG_HANDLE_HMAC_FLAG;


struct algorithm_impl
{
	BCRYPT_ALG_HANDLE handle;

	algorithm_impl (LPCWSTR id, DWORD flags, size_t expected_digest_size)
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

	~algorithm_impl () noexcept
	{
		::BCryptCloseAlgorithmProvider(handle, 0);
	}
};


} // namespace


context_type::context_type (const context_type &that) noexcept
{
	::BCryptDuplicateHash(
		that.handle,
		&handle,
		nullptr,
		0,
		0
	);
}


context_type::~context_type () noexcept
{
	if (handle)
	{
		::BCryptDestroyHash(handle);
	}
}


template <typename Algorithm, bool IsHMAC>
BCRYPT_ALG_HANDLE context_type::impl ()
{
	static algorithm_impl impl_
	{
		algorithm_id_v<Algorithm>,
		algorithm_flags_v<IsHMAC>,
		digest_size_v<Algorithm>
	};
	return impl_.handle;
}

template BCRYPT_ALG_HANDLE context_type::impl<md5, false> ();
template BCRYPT_ALG_HANDLE context_type::impl<md5, true> ();
template BCRYPT_ALG_HANDLE context_type::impl<sha1, false> ();
template BCRYPT_ALG_HANDLE context_type::impl<sha1, true> ();
template BCRYPT_ALG_HANDLE context_type::impl<sha256, false> ();
template BCRYPT_ALG_HANDLE context_type::impl<sha256, true> ();
template BCRYPT_ALG_HANDLE context_type::impl<sha384, false> ();
template BCRYPT_ALG_HANDLE context_type::impl<sha384, true> ();
template BCRYPT_ALG_HANDLE context_type::impl<sha512, false> ();
template BCRYPT_ALG_HANDLE context_type::impl<sha512, true> ();


template <typename Algorithm, bool IsHMAC>
BCRYPT_HASH_HANDLE context_type::make (const void *secret, size_t secret_size)
{
	BCRYPT_HASH_HANDLE handle;
	::BCryptCreateHash(
		impl<Algorithm, IsHMAC>(),
		&handle,
		nullptr, 0,
		static_cast<PUCHAR>(const_cast<void *>(secret)),
		static_cast<ULONG>(secret_size),
		BCRYPT_HASH_REUSABLE_FLAG
	);
	return handle;
}

template BCRYPT_HASH_HANDLE context_type::make<md5, false> (const void *, size_t);
template BCRYPT_HASH_HANDLE context_type::make<md5, true> (const void *, size_t);
template BCRYPT_HASH_HANDLE context_type::make<sha1, false> (const void *, size_t);
template BCRYPT_HASH_HANDLE context_type::make<sha1, true> (const void *, size_t);
template BCRYPT_HASH_HANDLE context_type::make<sha256, false> (const void *, size_t);
template BCRYPT_HASH_HANDLE context_type::make<sha256, true> (const void *, size_t);
template BCRYPT_HASH_HANDLE context_type::make<sha384, false> (const void *, size_t);
template BCRYPT_HASH_HANDLE context_type::make<sha384, true> (const void *, size_t);
template BCRYPT_HASH_HANDLE context_type::make<sha512, false> (const void *, size_t);
template BCRYPT_HASH_HANDLE context_type::make<sha512, true> (const void *, size_t);


void context_type::update (const void *data, size_t size)
{
	::BCryptHashData(
		handle,
		static_cast<PUCHAR>(const_cast<void *>(data)),
		static_cast<ULONG>(size),
		0
	);
}


void context_type::finish (void *digest, size_t size)
{
	::BCryptFinishHash(
		handle,
		static_cast<PUCHAR>(digest),
		static_cast<ULONG>(size),
		0
	);
}


} // namespace __bits


template <typename Algorithm>
hash<Algorithm>::hash ()
	: context_{__bits::context_type::make<Algorithm, false>()}
{ }


template <typename Algorithm>
void hash<Algorithm>::update (const void *input, size_t size) noexcept
{
	context_.update(input, size);
}


template <typename Algorithm>
void hash<Algorithm>::finish (void *result, size_t size) noexcept
{
	context_.finish(result, size);
}


#endif //}}}1


template class hash<md5>;
template class hash<sha1>;
template class hash<sha256>;
template class hash<sha384>;
template class hash<sha512>;


} // namespace crypto


__pal_end
