#include <pal/crypto/__crypto.hpp>

#if __pal_crypto_windows

// clang-format off
#include <pal/crypto/__certificate.hpp>
#include <algorithm>
#include <cstring>
// clang-format on

namespace pal::crypto
{

bool distinguished_name::impl_type::load_at (int index, oid_buffer &oid, value_buffer &value, entry &e) const noexcept
{
	if (index < 0 || index >= count)
	{
		return false;
	}

	const asn_decoder<::CERT_NAME_INFO, 2048> decoder{X509_NAME, name.pbData, name.cbData};
	if (!decoder.is_valid)
	{
		return false;
	}

	auto &attr = decoder.value.rgRDN[index].rgRDNAttr[0];

	auto len = std::min(std::strlen(attr.pszObjId), oid.size());
	std::copy_n(attr.pszObjId, len, oid.data());
	e.oid = {oid.data(), len};

	len = ::CertRDNValueToStr(attr.dwValueType, &attr.Value, value.data(), static_cast<DWORD>(value.size()));
	e.value = {value.data(), len > 0U ? len - 1 : 0};

	return true;
}

} // namespace pal::crypto

#endif // __pal_crypto_windows
