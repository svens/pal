#include <pal/__bits/platform_sdk>
#include <pal/crypto/hash>
#include <pal/expect>


__pal_begin


namespace crypto {


using __bits::md5_algorithm;
using __bits::sha1_algorithm;
using __bits::sha256_algorithm;
using __bits::sha384_algorithm;
using __bits::sha512_algorithm;


#if __pal_os_linux //{{{1


#define HASH_IMPL(Function, Type) \
	template <> \
	basic_hash<Type>::basic_hash () \
	{ \
		::Function ## _Init(&context_); \
	} \
	\
	template <> \
	void basic_hash<Type>::update (const void *input, size_t size) noexcept \
	{ \
		::Function ## _Update(&context_, input, size); \
	} \
	\
	template <> \
	void basic_hash<Type>::finish (void *result, size_t) noexcept \
	{ \
		::Function ## _Final(static_cast<uint8_t *>(result), &context_); \
		::Function ## _Init(&context_); \
	}

HASH_IMPL(MD5, md5_algorithm)
HASH_IMPL(SHA1, sha1_algorithm)
HASH_IMPL(SHA256, sha256_algorithm)
HASH_IMPL(SHA384, sha384_algorithm)
HASH_IMPL(SHA512, sha512_algorithm)


#elif __pal_os_macos //{{{1


#define HASH_IMPL(Function, Type) \
	template <> \
	basic_hash<Type>::basic_hash () \
	{ \
		::CC_ ## Function ## _Init(&context_); \
	} \
	\
	template <> \
	void basic_hash<Type>::update (const void *input, size_t size) noexcept \
	{ \
		::CC_ ## Function ## _Update(&context_, input, size); \
	} \
	\
	template <> \
	void basic_hash<Type>::finish (void *result, size_t) noexcept \
	{ \
		::CC_ ## Function ## _Final(static_cast<uint8_t *>(result), &context_); \
		::CC_ ## Function ## _Init(&context_); \
	}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
HASH_IMPL(MD5, md5_algorithm)
HASH_IMPL(SHA1, sha1_algorithm)
HASH_IMPL(SHA256, sha256_algorithm)
HASH_IMPL(SHA384, sha384_algorithm)
HASH_IMPL(SHA512, sha512_algorithm)
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
template <> constexpr LPCWSTR algorithm_id_v<md5_algorithm> = BCRYPT_MD5_ALGORITHM;
template <> constexpr LPCWSTR algorithm_id_v<sha1_algorithm> = BCRYPT_SHA1_ALGORITHM;
template <> constexpr LPCWSTR algorithm_id_v<sha256_algorithm> = BCRYPT_SHA256_ALGORITHM;
template <> constexpr LPCWSTR algorithm_id_v<sha384_algorithm> = BCRYPT_SHA384_ALGORITHM;
template <> constexpr LPCWSTR algorithm_id_v<sha512_algorithm> = BCRYPT_SHA512_ALGORITHM;


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

template BCRYPT_ALG_HANDLE context_type::impl<md5_algorithm, false> ();
template BCRYPT_ALG_HANDLE context_type::impl<md5_algorithm, true> ();
template BCRYPT_ALG_HANDLE context_type::impl<sha1_algorithm, false> ();
template BCRYPT_ALG_HANDLE context_type::impl<sha1_algorithm, true> ();
template BCRYPT_ALG_HANDLE context_type::impl<sha256_algorithm, false> ();
template BCRYPT_ALG_HANDLE context_type::impl<sha256_algorithm, true> ();
template BCRYPT_ALG_HANDLE context_type::impl<sha384_algorithm, false> ();
template BCRYPT_ALG_HANDLE context_type::impl<sha384_algorithm, true> ();
template BCRYPT_ALG_HANDLE context_type::impl<sha512_algorithm, false> ();
template BCRYPT_ALG_HANDLE context_type::impl<sha512_algorithm, true> ();


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

template BCRYPT_HASH_HANDLE context_type::make<md5_algorithm, false> (const void *, size_t);
template BCRYPT_HASH_HANDLE context_type::make<md5_algorithm, true> (const void *, size_t);
template BCRYPT_HASH_HANDLE context_type::make<sha1_algorithm, false> (const void *, size_t);
template BCRYPT_HASH_HANDLE context_type::make<sha1_algorithm, true> (const void *, size_t);
template BCRYPT_HASH_HANDLE context_type::make<sha256_algorithm, false> (const void *, size_t);
template BCRYPT_HASH_HANDLE context_type::make<sha256_algorithm, true> (const void *, size_t);
template BCRYPT_HASH_HANDLE context_type::make<sha384_algorithm, false> (const void *, size_t);
template BCRYPT_HASH_HANDLE context_type::make<sha384_algorithm, true> (const void *, size_t);
template BCRYPT_HASH_HANDLE context_type::make<sha512_algorithm, false> (const void *, size_t);
template BCRYPT_HASH_HANDLE context_type::make<sha512_algorithm, true> (const void *, size_t);


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
basic_hash<Algorithm>::basic_hash ()
	: context_{__bits::context_type::make<Algorithm, false>()}
{ }


template <typename Algorithm>
void basic_hash<Algorithm>::update (const void *input, size_t size) noexcept
{
	context_.update(input, size);
}


template <typename Algorithm>
void basic_hash<Algorithm>::finish (void *result, size_t size) noexcept
{
	context_.finish(result, size);
}


#endif //}}}1


template class basic_hash<md5_algorithm>;
template class basic_hash<sha1_algorithm>;
template class basic_hash<sha256_algorithm>;
template class basic_hash<sha384_algorithm>;
template class basic_hash<sha512_algorithm>;


} // namespace crypto


__pal_end
