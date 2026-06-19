#include <pal/crypto/__crypto.hpp>

#if __pal_crypto_openssl

#include <pal/crypto/__certificate.hpp>
#include <algorithm>

namespace pal::crypto
{

bool distinguished_name::impl_type::load_at (int index, oid_buffer &oid, value_buffer &value, entry &e) const noexcept
{
	if (index < 0 || index >= count)
	{
		return false;
	}

	auto *p = ::X509_NAME_get_entry(name, index);

	const auto *o = ::X509_NAME_ENTRY_get_object(p);
	auto len = ::OBJ_obj2txt(oid.data(), static_cast<int>(oid.size()), o, 1);
	e.oid = {oid.data(), static_cast<size_t>(len)};

	const auto *v = ::X509_NAME_ENTRY_get_data(p);
	len = std::min(::ASN1_STRING_length(v), static_cast<int>(value.size()));
	std::copy_n(::ASN1_STRING_get0_data(v), len, value.data());
	e.value = {value.data(), static_cast<size_t>(len)};

	return true;
}

} // namespace pal::crypto

#endif // __pal_crypto_openssl
