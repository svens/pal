#include <pal/crypto/__crypto.hpp>

#if __pal_crypto_openssl

// clang-format off
#include <pal/crypto/__certificate.hpp>
#include <pal/codec.hpp>
#include <pal/memory.hpp>
#include <pal/net/ip/address_v4.hpp>
#include <pal/net/ip/address_v6.hpp>
#include <algorithm>
#include <ctime>
// clang-format on

namespace pal::crypto
{

namespace
{

certificate::time_type to_time (const ::ASN1_TIME *time) noexcept
{
	std::tm tm{};
	::ASN1_TIME_to_tm(time, &tm);
	return certificate::clock_type::from_time_t(std::mktime(&tm));
}

general_names_ptr get_alternative_name (::X509 *x509, int nid) noexcept
{
	return general_names_ptr{static_cast<::GENERAL_NAMES *>(::X509_get_ext_d2i(x509, nid, nullptr, nullptr))};
}

} // namespace

certificate::impl_type::impl_type (cert_ptr x509) noexcept
	: x509{std::move(x509)}
	, bytes_buf{std::nothrow, static_cast<size_t>(::i2d_X509(this->x509.get(), nullptr))}
	, bytes{init_bytes_from_cert()}
	, serial_number{init_serial_number()}
	, common_name{init_common_name()}
	, fingerprint{init_fingerprint()}
	, not_before{to_time(::X509_get_notBefore(this->x509.get()))}
	, not_after{to_time(::X509_get_notAfter(this->x509.get()))}
	, subject_dn{::X509_get_subject_name(this->x509.get())}
	, issuer_dn{::X509_get_issuer_name(this->x509.get())}
	, subject_san_value{init_san_value()}
{
}

certificate::impl_type::impl_type (cert_ptr x509, std::span<const std::byte> der)
	: x509{std::move(x509)}
	, bytes_buf{der.size()}
	, bytes{init_bytes(der)}
	, serial_number{init_serial_number()}
	, common_name{init_common_name()}
	, fingerprint{init_fingerprint()}
	, not_before{to_time(::X509_get_notBefore(this->x509.get()))}
	, not_after{to_time(::X509_get_notAfter(this->x509.get()))}
	, subject_dn{::X509_get_subject_name(this->x509.get())}
	, issuer_dn{::X509_get_issuer_name(this->x509.get())}
	, subject_san_value{init_san_value()}
{
}

std::span<const std::byte> certificate::impl_type::init_bytes_from_cert () noexcept
{
	if (auto *p = reinterpret_cast<uint8_t *>(bytes_buf.get().data()))
	{
		if (const auto written = ::i2d_X509(x509.get(), &p); written > 0)
		{
			return std::as_bytes(bytes_buf.get().first(written));
		}
	}
	return {};
}

std::span<const std::byte> certificate::impl_type::init_bytes (std::span<const std::byte> der) noexcept
{
	std::ranges::copy(der, bytes_buf.get().begin());
	return bytes_buf.get();
}

std::span<const uint8_t> certificate::impl_type::init_serial_number () const noexcept
{
	const auto *sn = ::X509_get0_serialNumber(x509.get());
	return {::ASN1_STRING_get0_data(sn), static_cast<size_t>(::ASN1_STRING_length(sn))};
}

std::string_view certificate::impl_type::init_common_name () const noexcept
{
	auto *subject = ::X509_get_subject_name(x509.get());
	for (int i = 0; i < ::X509_NAME_entry_count(subject); ++i)
	{
		auto *entry = ::X509_NAME_get_entry(subject, i);
		if (::OBJ_obj2nid(::X509_NAME_ENTRY_get_object(entry)) == NID_commonName)
		{
			return reinterpret_cast<const char *>(
				::ASN1_STRING_get0_data(::X509_NAME_ENTRY_get_data(entry))
			);
		}
	}
	return {};
}

alternative_name_value certificate::impl_type::init_san_value () const noexcept
{
	alternative_name_value result{};

	auto names = get_alternative_name(x509.get(), NID_subject_alt_name);
	if (!names)
	{
		return result;
	}

	// clang-format off

	static constexpr auto to_dns_view = [] (const ASN1_STRING *s) noexcept -> std::string_view
	{
		return
		{
			reinterpret_cast<const char *>(::ASN1_STRING_get0_data(s)),
			static_cast<size_t>(::ASN1_STRING_length(s))
		};
	};

	std::array<char, net::ip::address_v6::max_string_length + 1> buf{};
	const auto to_ip_view = [&buf] (const ASN1_STRING *s) noexcept -> std::string_view
	{
		const auto *bytes = reinterpret_cast<const uint8_t *>(::ASN1_STRING_get0_data(s));
		const auto size = static_cast<size_t>(::ASN1_STRING_length(s));
		const char *end = nullptr;
		if (size == sizeof(net::ip::address_v4::bytes_type))
		{
			end = net::ip::__address_v4::ntop(bytes, buf.data(), buf.data() + buf.size());
		}
		else if (size == sizeof(net::ip::address_v6::bytes_type))
		{
			end = net::ip::__address_v6::ntop(bytes, buf.data(), buf.data() + buf.size());
		}
		if (end != nullptr)
		{
			return {buf.data(), static_cast<size_t>(end - buf.data())};
		}
		return {};
	};

	// clang-format on

	auto *p = result.data_.data();
	const auto count = sk_GENERAL_NAME_num(names.get());
	for (auto i = 0; i < count && p != nullptr; ++i)
	{
		const auto *gen = sk_GENERAL_NAME_value(names.get(), i);
		if (gen->type == GEN_DNS)
		{
			if (const auto sv = to_dns_view(gen->d.dNSName); !sv.empty())
			{
				p = result.append(p, alternative_name_value::kind::fqdn, sv);
			}
		}
		else if (gen->type == GEN_IPADD)
		{
			if (const auto sv = to_ip_view(gen->d.iPAddress); !sv.empty())
			{
				p = result.append(p, alternative_name_value::kind::ip, sv);
			}
		}
	}

	return result;
}

std::string_view certificate::impl_type::init_fingerprint () noexcept
{
	std::array<unsigned char, sha1_hex_size / 2> digest{};
	unsigned int len = digest.size();
	::X509_digest(x509.get(), ::EVP_sha1(), digest.data(), &len);
	convert(hex_encode, fingerprint_buf, std::as_bytes(std::span{digest}));
	return {fingerprint_buf.data(), fingerprint_buf.size()};
}

result<certificate> certificate::import_der (std::span<const std::byte> der) noexcept
{
	const auto *data = reinterpret_cast<const uint8_t *>(der.data());
	if (cert_ptr x509{::d2i_X509(nullptr, &data, static_cast<long>(der.size()))})
	{
		return pal::make_shared<impl_type>(std::move(x509), der).transform(certificate::to_api);
	}
	return make_unexpected(std::errc::invalid_argument);
}

std::span<const std::byte> certificate::as_bytes () const noexcept
{
	return impl_->bytes;
}

int certificate::version () const noexcept
{
	return static_cast<int>(::X509_get_version(impl_->x509.get())) + 1;
}

std::span<const uint8_t> certificate::serial_number () const noexcept
{
	return impl_->serial_number;
}

std::string_view certificate::common_name () const noexcept
{
	return impl_->common_name;
}

std::string_view certificate::fingerprint () const noexcept
{
	return impl_->fingerprint;
}

certificate::time_type certificate::not_before () const noexcept
{
	return impl_->not_before;
}

certificate::time_type certificate::not_after () const noexcept
{
	return impl_->not_after;
}

bool certificate::is_issued_by (const certificate &that) const noexcept
{
	return ::X509_check_issued(that.impl_->x509.get(), impl_->x509.get()) == X509_V_OK;
}

bool certificate::is_self_signed () const noexcept
{
	return ::X509_self_signed(impl_->x509.get(), 0) == 1;
}

result<alternative_name> certificate::subject_alternative_name () const noexcept
{
	if (auto names = get_alternative_name(impl_->x509.get(), NID_subject_alt_name))
	{
		return pal::make_shared<alternative_name::impl_type>(impl_, std::move(names))
			.transform(alternative_name::to_api);
	}
	return alternative_name{nullptr};
}

result<alternative_name> certificate::issuer_alternative_name () const noexcept
{
	if (auto names = get_alternative_name(impl_->x509.get(), NID_issuer_alt_name))
	{
		return pal::make_shared<alternative_name::impl_type>(impl_, std::move(names))
			.transform(alternative_name::to_api);
	}
	return alternative_name{nullptr};
}

result<key> certificate::public_key () const noexcept
{
	auto &pkey = *::X509_get0_pubkey(impl_->x509.get());
	return pal::make_shared<key::impl_type>(impl_, pkey).transform(key::to_api);
}

result<key> certificate::private_key () const noexcept
{
	if (impl_->private_key)
	{
		return pal::make_shared<key::impl_type>(impl_, *impl_->private_key).transform(key::to_api);
	}
	return make_unexpected(std::errc::io_error);
}

} // namespace pal::crypto

#endif // __pal_crypto_openssl
