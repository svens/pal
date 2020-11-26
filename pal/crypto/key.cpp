#include <pal/__bits/platform_sdk>
#include <pal/crypto/key>

#if __pal_os_linux //{{{1

#elif __pal_os_macos //{{{1
	#include <Security/SecItem.h>
#elif __pal_os_windows //{{{1

#endif //}}}1


__pal_begin


namespace crypto {


#if __pal_os_linux //{{{1


namespace {


void query_properties (const __bits::public_key &key,
	key_algorithm &algorithm,
	size_t &size_bits) noexcept
{
	size_bits = 8 * EVP_PKEY_size(key.ref);

	switch (EVP_PKEY_id(key.ref))
	{
		case EVP_PKEY_RSA:
			algorithm = key_algorithm::rsa;
			break;
	}
}


} // namespace


#elif __pal_os_macos //{{{1


namespace {


void query_properties (const __bits::public_key &key,
	key_algorithm &algorithm,
	size_t &size_bits) noexcept
{
	unique_ref<::CFDictionaryRef> dir = ::SecKeyCopyAttributes(key.ref);
	::CFTypeRef value;

	// size_bits
	if (::CFDictionaryGetValueIfPresent(dir.ref, ::kSecAttrKeySizeInBits, &value))
	{
		::CFNumberGetValue((::CFNumberRef)value, ::kCFNumberSInt64Type, &size_bits);
	}

	// algorithm
	if (::CFDictionaryGetValueIfPresent(dir.ref, kSecAttrKeyType, &value))
	{
		if (::CFEqual(value, kSecAttrKeyTypeRSA))
		{
			algorithm = key_algorithm::rsa;
		}
	}
}


} // namespace


#elif __pal_os_windows //{{{1


namespace {


void query_properties (const __bits::public_key &key,
	key_algorithm &algorithm,
	size_t &size_bits) noexcept
{
	// size_bits
	{
		DWORD buf;
		ULONG buf_size;
		::BCryptGetProperty(
			key.ref,
			BCRYPT_BLOCK_LENGTH,
			reinterpret_cast<PUCHAR>(&buf),
			sizeof(buf),
			&buf_size,
			0
		);
		size_bits = 8 * buf;
	}

	// algorithm
	{
		wchar_t buf[256];
		ULONG buf_size;
		::BCryptGetProperty(
			key.ref,
			BCRYPT_ALGORITHM_NAME,
			reinterpret_cast<PUCHAR>(buf),
			sizeof(buf),
			&buf_size,
			0
		);
		reinterpret_cast<char *>(buf)[buf_size] = '\0';
		if (wcscmp(buf, BCRYPT_RSA_ALGORITHM) == 0)
		{
			algorithm = key_algorithm::rsa;
		}
	}
}


} // namespace


#endif //}}}1


public_key::public_key (__bits::public_key &&key) noexcept
	: basic_key(std::forward<__bits::public_key>(key))
{
	query_properties(key_, algorithm_, size_bits_);
}


} // namespace crypto


__pal_end
