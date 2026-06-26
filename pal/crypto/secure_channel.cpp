#include <pal/crypto/secure_channel.hpp>
#include <string>

namespace pal::crypto
{

namespace
{

constexpr std::string_view as_view (secure_channel_errc ec) noexcept
{
	switch (ec)
	{
#define __pal_secure_channel_errc_case(Code, Message) case secure_channel_errc::Code: return Message;
		__pal_secure_channel_errc(__pal_secure_channel_errc_case)
#undef __pal_secure_channel_errc_case
	}
	return "unknown secure channel error";
}

} // namespace

const std::error_category &secure_channel_category () noexcept
{
	struct impl_type: std::error_category
	{
		[[nodiscard]] const char *name () const noexcept final
		{
			return "secure_channel";
		}

		[[nodiscard]] std::string message (int ec) const final
		{
			return std::string{as_view(static_cast<secure_channel_errc>(ec))};
		}
	};
	static const impl_type impl{};
	return impl;
}

} // namespace pal::crypto
