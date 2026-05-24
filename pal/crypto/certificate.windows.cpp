#include <pal/crypto/__crypto.hpp>

#if __pal_crypto_windows

// clang-format off
#include <pal/crypto/__certificate.hpp>
#include <pal/memory.hpp>
// clang-format on

namespace pal::crypto
{

result<certificate> certificate::import_der (std::span<const std::byte> der) noexcept
{
	auto ptr = reinterpret_cast<const BYTE *>(der.data());
	auto len = static_cast<DWORD>(der.size());
	if (cert_ptr x509{::CertCreateCertificateContext(X509_ASN_ENCODING, ptr, len)})
	{
		return pal::make_shared<impl_type>(std::move(x509)).transform(impl_type::to_api);
	}
	return make_unexpected(std::errc::invalid_argument);
}

} // namespace pal::crypto

#endif // __pal_crypto_windows
