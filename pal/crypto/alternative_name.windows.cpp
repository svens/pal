#include <pal/crypto/__crypto.hpp>

#if __pal_crypto_windows

// clang-format off
#include <pal/crypto/__certificate.hpp>
// clang-format on

namespace pal::crypto
{

bool alternative_name::impl_type::load_at (int, entry_buffer &, alternative_name_entry &) const noexcept
{
	return false;
}

alternative_name certificate::subject_alternative_name () const noexcept
{
	return alternative_name{nullptr};
}

alternative_name certificate::issuer_alternative_name () const noexcept
{
	return alternative_name{nullptr};
}

} // namespace pal::crypto

#endif // __pal_crypto_windows
