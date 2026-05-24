#include <pal/crypto/__crypto.hpp>

#if __pal_crypto_openssl

// clang-format off
#include <pal/crypto/__certificate.hpp>
#include <pal/memory.hpp>
// clang-format on

namespace pal::crypto
{

result<certificate> certificate::import_der (std::span<const std::byte> der) noexcept
{
	const auto *data = reinterpret_cast<const uint8_t *>(der.data());
	if (cert_ptr x509{::d2i_X509(nullptr, &data, static_cast<long>(der.size()))})
	{
		return pal::make_shared<impl_type>(std::move(x509)).transform(impl_type::to_api);
	}
	return make_unexpected(std::errc::invalid_argument);
}

} // namespace pal::crypto

#endif // __pal_crypto_openssl
