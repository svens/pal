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
	const auto result = ::BCryptGenRandom(
		nullptr,
		reinterpret_cast<PUCHAR>(buf.data()),
		static_cast<ULONG>(buf.size()),
		BCRYPT_USE_SYSTEM_PREFERRED_RNG
	);

	// LCOV_EXCL_START
	if (result != STATUS_SUCCESS)
	{
		return make_unexpected(std::errc::io_error);
	}
	// LCOV_EXCL_STOP

	return {};
}

} // namespace pal::crypto::__crypto

#endif // __pal_crypto_windows
