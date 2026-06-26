#include <pal/crypto/__crypto.hpp>

#if __pal_crypto_windows

#include <pal/crypto/__certificate.hpp>
#include <pal/net/ip/address_v4.hpp>
#include <pal/net/ip/address_v6.hpp>

namespace pal::crypto
{

namespace
{

size_t to_utf8 (::LPCWSTR wide, char *buf, size_t size) noexcept
{
	auto len = ::WideCharToMultiByte(CP_UTF8, 0, wide, -1, buf, static_cast<int>(size), nullptr, nullptr);
	return len > 0 ? static_cast<size_t>(len - 1) : 0;
}

} // namespace

bool alternative_name::impl_type::load_at (int index, entry_buffer &buf, alternative_name_entry &entry) const noexcept
{
	const auto &alt = name.value.rgAltEntry[index];
	switch (alt.dwAltNameChoice)
	{
		case CERT_ALT_NAME_DNS_NAME:
			if (auto len = to_utf8(alt.pwszDNSName, buf.data(), buf.size()))
			{
				entry.emplace<alt_name::dns>(buf.data(), len);
				return true;
			}
			return false;

		case CERT_ALT_NAME_RFC822_NAME:
			if (auto len = to_utf8(alt.pwszRfc822Name, buf.data(), buf.size()))
			{
				entry.emplace<alt_name::email>(buf.data(), len);
				return true;
			}
			return false;

		case CERT_ALT_NAME_URL:
			if (auto len = to_utf8(alt.pwszURL, buf.data(), buf.size()))
			{
				entry.emplace<alt_name::uri>(buf.data(), len);
				return true;
			}
			return false;

		case CERT_ALT_NAME_IP_ADDRESS:
		{
			const auto *bytes = alt.IPAddress.pbData;
			const auto byte_count = alt.IPAddress.cbData;
			char *end = nullptr;
			if (byte_count == sizeof(net::ip::address_v4::bytes_type))
			{
				end = net::ip::__address_v4::ntop(bytes, buf.data(), buf.data() + buf.size());
			}
			else if (byte_count == sizeof(net::ip::address_v6::bytes_type))
			{
				end = net::ip::__address_v6::ntop(bytes, buf.data(), buf.data() + buf.size());
			}
			if (end != nullptr)
			{
				entry.emplace<alt_name::ip>(buf.data(), end - buf.data());
				return true;
			}
			return false;
		}

		default:
			return false;
	}
}

} // namespace pal::crypto

#endif // __pal_crypto_windows
