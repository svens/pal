#include <pal/__bits/platform_sdk>
#include <pal/crypto/key>

#if __pal_os_macos //{{{1
	#include <algorithm>
	#include <Security/SecItem.h>
#elif __pal_os_windows //{{{1
	#include <pal/crypto/hash>
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


inline const EVP_MD *to_algorithm (size_t digest_algorithm) noexcept
{
	switch (digest_algorithm)
	{
		case __bits::digest_algorithm_v<__bits::sha1_algorithm>:
			return EVP_sha1();
		case __bits::digest_algorithm_v<__bits::sha256_algorithm>:
			return EVP_sha256();
		case __bits::digest_algorithm_v<__bits::sha384_algorithm>:
			return EVP_sha384();
		case __bits::digest_algorithm_v<__bits::sha512_algorithm>:
			return EVP_sha512();
	}
	return nullptr;
}


} // namespace


std::span<std::byte> private_key::sign (
	size_t digest_algorithm,
	const std::span<const std::byte> &message,
	std::span<std::byte> &&signature) noexcept
{
	auto algorithm = to_algorithm(digest_algorithm);
	if (!algorithm)
	{
		return {};
	}

	size_t sig_size = 0;
	unique_ref<::EVP_MD_CTX *, ::EVP_MD_CTX_free> ctx = EVP_MD_CTX_new();
	::EVP_DigestSignInit(ctx.ref, nullptr, algorithm, nullptr, key_.ref);
	::EVP_DigestSignUpdate(ctx.ref, message.data(), message.size());
	::EVP_DigestSignFinal(ctx.ref, nullptr, &sig_size);

	if (sig_size > signature.size())
	{
		return {static_cast<std::byte *>(0), sig_size};
	}

	::EVP_DigestSignFinal(
		ctx.ref,
		reinterpret_cast<uint8_t *>(signature.data()),
		&sig_size
	);

	return signature.first(sig_size);
}


bool public_key::verify_signature (
	size_t digest_algorithm,
	const std::span<const std::byte> &message,
	const std::span<const std::byte> &signature) noexcept
{
	auto algorithm = to_algorithm(digest_algorithm);
	if (!algorithm)
	{
		return {};
	}

	unique_ref<::EVP_MD_CTX *, ::EVP_MD_CTX_free> ctx = EVP_MD_CTX_new();
	::EVP_DigestVerifyInit(ctx.ref, nullptr, algorithm, nullptr, key_.ref);
	::EVP_DigestVerifyUpdate(ctx.ref, message.data(), message.size());

	return ::EVP_DigestVerifyFinal(
		ctx.ref,
		reinterpret_cast<const uint8_t *>(signature.data()),
		signature.size()
	);
}


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


inline ::SecKeyAlgorithm to_algorithm (key_algorithm type, size_t digest_algorithm) noexcept
{
	if (type == key_algorithm::rsa)
	{
		switch (digest_algorithm)
		{
			case __bits::digest_algorithm_v<__bits::sha1_algorithm>:
				return ::kSecKeyAlgorithmRSASignatureMessagePKCS1v15SHA1;
			case __bits::digest_algorithm_v<__bits::sha256_algorithm>:
				return ::kSecKeyAlgorithmRSASignatureMessagePKCS1v15SHA256;
			case __bits::digest_algorithm_v<__bits::sha384_algorithm>:
				return ::kSecKeyAlgorithmRSASignatureMessagePKCS1v15SHA384;
			case __bits::digest_algorithm_v<__bits::sha512_algorithm>:
				return ::kSecKeyAlgorithmRSASignatureMessagePKCS1v15SHA512;
		}
	}
	return {};
}


inline unique_ref<::CFDataRef> make_data (
	const std::span<const std::byte> &bytes) noexcept
{
	return ::CFDataCreateWithBytesNoCopy(
		nullptr,
		reinterpret_cast<const uint8_t *>(bytes.data()),
		bytes.size(),
		::kCFAllocatorNull
	);
}


} // namespace


std::span<std::byte> private_key::sign (
	size_t digest_algorithm,
	const std::span<const std::byte> &message,
	std::span<std::byte> &&signature) noexcept
{
	auto algorithm = to_algorithm(algorithm_, digest_algorithm);
	if (!algorithm)
	{
		return {};
	}

	unique_ref<::CFErrorRef> status;
	unique_ref<::CFDataRef> sig = ::SecKeyCreateSignature(
		key_.ref,
		algorithm,
		make_data(message).ref,
		&status.ref
	);

	size_t sig_size = ::CFDataGetLength(sig.ref);
	if (sig_size <= signature.size())
	{
		auto sig_ptr = ::CFDataGetBytePtr(sig.ref);
		std::uninitialized_copy(
			sig_ptr,
			sig_ptr + sig_size,
			reinterpret_cast<uint8_t *>(signature.data())
		);
		return signature.first(sig_size);
	}

	return {static_cast<std::byte *>(0), sig_size};
}


bool public_key::verify_signature (
	size_t digest_algorithm,
	const std::span<const std::byte> &message,
	const std::span<const std::byte> &signature) noexcept
{
	auto algorithm = to_algorithm(algorithm_, digest_algorithm);
	if (!algorithm)
	{
		return {};
	}

	unique_ref<CFErrorRef> status;
	auto result = ::SecKeyVerifySignature(
		key_.ref,
		algorithm,
		make_data(message).ref,
		make_data(signature).ref,
		&status.ref
	);

	if (status.ref == nullptr || ::CFErrorGetCode(status.ref) == ::errSecVerifyFailed)
	{
		return result;
	}

	return false;
}


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


void query_properties (const __bits::private_key &key,
	key_algorithm &algorithm,
	size_t &size_bits) noexcept
{
	// size_bits
	{
		DWORD buf = 0;
		ULONG buf_size;
		::NCryptGetProperty(
			key.ref,
			NCRYPT_BLOCK_LENGTH_PROPERTY,
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
		::NCryptGetProperty(
			key.ref,
			NCRYPT_ALGORITHM_GROUP_PROPERTY,
			reinterpret_cast<PUCHAR>(buf),
			sizeof(buf),
			&buf_size,
			0
		);
		reinterpret_cast<char *>(buf)[buf_size] = '\0';
		if (wcscmp(buf, NCRYPT_RSA_ALGORITHM_GROUP) == 0)
		{
			algorithm = key_algorithm::rsa;
		}
	}
}


template <typename Hash>
inline DWORD make_digest (
	const std::span<const std::byte> &message,
	uint8_t *digest) noexcept
{
	reinterpret_cast<typename Hash::result_type &>(*digest) = Hash::one_shot(message);
	return Hash::digest_size;
}


const wchar_t *get_algorithm_and_digest (
	size_t digest_algorithm,
	const std::span<const std::byte> &message,
	uint8_t *digest,
	DWORD &digest_size) noexcept
{
	switch (digest_algorithm)
	{
		case __bits::digest_algorithm_v<__bits::sha1_algorithm>:
			digest_size = make_digest<sha1_hash>(message, digest);
			return NCRYPT_SHA1_ALGORITHM;

		case __bits::digest_algorithm_v<__bits::sha256_algorithm>:
			digest_size = make_digest<sha256_hash>(message, digest);
			return NCRYPT_SHA256_ALGORITHM;

		case __bits::digest_algorithm_v<__bits::sha384_algorithm>:
			digest_size = make_digest<sha384_hash>(message, digest);
			return NCRYPT_SHA384_ALGORITHM;

		case __bits::digest_algorithm_v<__bits::sha512_algorithm>:
			digest_size = make_digest<sha512_hash>(message, digest);
			return NCRYPT_SHA512_ALGORITHM;
	}
	return {};
}


} // namespace


std::span<std::byte> private_key::sign (
	size_t digest_algorithm,
	const std::span<const std::byte> &message,
	std::span<std::byte> &&signature) noexcept
{
	uint8_t digest[1024];
	DWORD digest_size;

	BCRYPT_PKCS1_PADDING_INFO padding;
	padding.pszAlgId = get_algorithm_and_digest(
		digest_algorithm,
		message,
		digest,
		digest_size
	);
	if (!padding.pszAlgId)
	{
		return {};
	}

	auto sig_size = static_cast<DWORD>(signature.size());
	auto status = ::NCryptSignHash(
		key_.ref,
		&padding,
		digest,
		digest_size,
		reinterpret_cast<PBYTE>(signature.data()),
		sig_size,
		&sig_size,
		BCRYPT_PAD_PKCS1
	);

	if (status == ERROR_SUCCESS)
	{
		return signature.first(sig_size);
	}
	else if (status == NTE_BUFFER_TOO_SMALL)
	{
		return {static_cast<std::byte *>(0), sig_size};
	}

	return {};
}


bool public_key::verify_signature (
	size_t digest_algorithm,
	const std::span<const std::byte> &message,
	const std::span<const std::byte> &signature) noexcept
{
	uint8_t digest[1024];
	DWORD digest_size;

	BCRYPT_PKCS1_PADDING_INFO padding;
	padding.pszAlgId = get_algorithm_and_digest(
		digest_algorithm,
		message,
		digest,
		digest_size
	);
	if (!padding.pszAlgId)
	{
		return false;
	}

	auto status = ::BCryptVerifySignature(
		key_.ref,
		&padding,
		digest,
		digest_size,
		reinterpret_cast<PUCHAR>(const_cast<std::byte *>(signature.data())),
		static_cast<DWORD>(signature.size()),
		BCRYPT_PAD_PKCS1
	);

	return status == STATUS_SUCCESS;
}


#endif //}}}1


public_key::public_key (__bits::public_key &&key) noexcept
	: basic_key(std::forward<__bits::public_key>(key))
{
	if (key_)
	{
		query_properties(key_, algorithm_, size_bits_);
	}
}


private_key::private_key (__bits::private_key &&key) noexcept
	: basic_key(std::forward<__bits::private_key>(key))
{
	if (key_)
	{
		query_properties(key_, algorithm_, size_bits_);
	}
}


} // namespace crypto


__pal_end
