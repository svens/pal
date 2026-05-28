#include <pal/crypto/__crypto.hpp>

#if __pal_crypto_windows

// clang-format off
#include <pal/crypto/__certificate.hpp>
#include <pal/memory.hpp>
#include <bcrypt.h>
// clang-format on

namespace pal::crypto
{

namespace
{

key_algorithm algorithm_from_name (const wchar_t *name) noexcept
{
	if (::wcscmp(name, BCRYPT_RSA_ALGORITHM) == 0)
	{
		return key_algorithm::rsa;
	}
	if (::wcsncmp(name, L"EC", 2) == 0)
	{
		return key_algorithm::ec;
	}
	return key_algorithm::opaque;
}

key_algorithm to_algorithm (::BCRYPT_KEY_HANDLE pkey) noexcept
{
	wchar_t buf[64] = {};
	::ULONG len = 0;
	::BCryptGetProperty(
		pkey, BCRYPT_ALGORITHM_NAME, reinterpret_cast<::PUCHAR>(buf), sizeof(buf) - sizeof(wchar_t), &len, 0
	);
	return algorithm_from_name(buf);
}

key_algorithm to_algorithm (::NCRYPT_KEY_HANDLE pkey) noexcept
{
	wchar_t buf[64] = {};
	::ULONG len = 0;
	::NCryptGetProperty(
		pkey,
		NCRYPT_ALGORITHM_GROUP_PROPERTY,
		reinterpret_cast<::PUCHAR>(buf),
		sizeof(buf) - sizeof(wchar_t),
		&len,
		0
	);
	return algorithm_from_name(buf);
}

size_t get_size (::BCRYPT_KEY_HANDLE pkey, ::LPCWSTR property) noexcept
{
	::DWORD buf = 0;
	::ULONG len = 0;
	::BCryptGetProperty(pkey, property, reinterpret_cast<::PUCHAR>(&buf), sizeof(buf), &len, 0);
	return buf;
}

size_t get_size (::NCRYPT_KEY_HANDLE pkey, ::LPCWSTR property) noexcept
{
	::DWORD buf = 0;
	::ULONG len = 0;
	::NCryptGetProperty(pkey, property, reinterpret_cast<::PUCHAR>(&buf), sizeof(buf), &len, 0);
	return buf;
}

constexpr size_t max_block_size_for (key_algorithm algorithm, size_t size_bits) noexcept
{
	if (algorithm == key_algorithm::ec)
	{
		const auto n = (size_bits + 7) / 8;
		const auto content = 2 * (n + 3);
		return content > 127 ? 3 + content : 2 + content;
	}
	return size_bits / 8;
}

} // namespace

key::impl_type::impl_type (certificate::impl_ptr owner, ::BCRYPT_KEY_HANDLE pkey) noexcept
	: owner{std::move(owner)}
	, pkey{pkey}
	, algorithm{to_algorithm(pkey)}
	, size_bits{get_size(pkey, BCRYPT_KEY_LENGTH)}
	, max_block_size{max_block_size_for(algorithm, size_bits)}
{
}

key::impl_type::impl_type (certificate::impl_ptr owner, ::NCRYPT_KEY_HANDLE pkey) noexcept
	: owner{std::move(owner)}
	, pkey{pkey}
	, algorithm{to_algorithm(pkey)}
	, size_bits{get_size(pkey, NCRYPT_LENGTH_PROPERTY)}
	, max_block_size{max_block_size_for(algorithm, size_bits)}
{
}

key::impl_type::~impl_type () noexcept
{
	if (auto *bh = std::get_if<::BCRYPT_KEY_HANDLE>(&pkey))
	{
		::BCryptDestroyKey(*bh);
	}
	// NCRYPT handle is borrowed from certificate::impl_type; lifetime managed there
}

key_algorithm key::algorithm () const noexcept
{
	return impl_->algorithm;
}

size_t key::size_bits () const noexcept
{
	return impl_->size_bits;
}

size_t key::max_block_size () const noexcept
{
	return impl_->max_block_size;
}

} // namespace pal::crypto

#endif // __pal_crypto_windows
