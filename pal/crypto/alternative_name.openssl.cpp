#include <pal/crypto/__crypto.hpp>

#if __pal_crypto_openssl

// clang-format off
#include <pal/crypto/__certificate.hpp>
#include <pal/net/ip/address_v4.hpp>
#include <pal/net/ip/address_v6.hpp>
#include <algorithm>
// clang-format on

namespace pal::crypto
{

bool alternative_name::impl_type::load_at (int index, entry_buffer &buf, alternative_name_entry &entry) const noexcept
{
	const auto *gen = sk_GENERAL_NAME_value(names.get(), index);
	switch (gen->type)
	{
		case GEN_DNS:
		{
			auto len = std::min<size_t>(::ASN1_STRING_length(gen->d.dNSName), buf.size());
			std::copy_n(::ASN1_STRING_get0_data(gen->d.dNSName), len, buf.data());
			entry.emplace<dns_name>(buf.data(), len);
			return true;
		}

		case GEN_EMAIL:
		{
			auto len = std::min<size_t>(::ASN1_STRING_length(gen->d.rfc822Name), buf.size());
			std::copy_n(::ASN1_STRING_get0_data(gen->d.rfc822Name), len, buf.data());
			entry.emplace<email_address>(buf.data(), len);
			return true;
		}

		case GEN_URI:
		{
			auto len = std::min<size_t>(::ASN1_STRING_length(gen->d.uniformResourceIdentifier), buf.size());
			std::copy_n(::ASN1_STRING_get0_data(gen->d.uniformResourceIdentifier), len, buf.data());
			entry.emplace<uri>(buf.data(), len);
			return true;
		}

		case GEN_IPADD:
		{
			const auto *bytes = ::ASN1_STRING_get0_data(gen->d.iPAddress);
			const auto byte_count = ::ASN1_STRING_length(gen->d.iPAddress);
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
				entry.emplace<ip_address>(buf.data(), end - buf.data());
				return true;
			}
			return false;
		}

		default:
			return false;
	}
}

} // namespace pal::crypto

#endif // __pal_crypto_openssl
