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
		case secure_channel_errc::handshake_failed:
			return "TLS handshake failed";
		case secure_channel_errc::peer_verification_failed:
			return "peer certificate verification failed";
		case secure_channel_errc::peer_hostname_mismatch:
			return "peer hostname does not match certificate";
		case secure_channel_errc::no_application_protocol:
			return "no overlapping ALPN protocol";
		case secure_channel_errc::client_certificate_required:
			return "client certificate required but not provided";
		case secure_channel_errc::message_too_large:
			return "message exceeds datagram MTU";
		case secure_channel_errc::decrypt_failed:
			return "record decryption failed";
		case secure_channel_errc::protocol_error:
			return "secure channel protocol error";
		case secure_channel_errc::closed:
			return "secure channel is closed";
		case secure_channel_errc::invalid_configuration:
			return "invalid secure channel configuration";
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
