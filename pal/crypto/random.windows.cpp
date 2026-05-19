#include <pal/crypto/__crypto.hpp>

#if __pal_crypto_windows

// clang-format off
#include <pal/crypto/random.hpp>
#include <bcrypt.h>
// clang-format on

namespace pal::crypto::__crypto
{

result<void> random_fill (std::span<std::byte> buf) noexcept
{
	auto status = ::BCryptGenRandom(
		nullptr,
		reinterpret_cast<PUCHAR>(buf.data()),
		static_cast<ULONG>(buf.size()),
		BCRYPT_USE_SYSTEM_PREFERRED_RNG
	);
	if (status == STATUS_SUCCESS)
	{
		return {};
	}
	return make_unexpected(std::errc::io_error);
}

} // namespace pal::crypto::__crypto

#endif // __pal_crypto_windows
