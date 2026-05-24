#include <pal/crypto/__crypto.hpp>

#if __pal_crypto_openssl

// clang-format off
#include <pal/crypto/__certificate.hpp>
#include <pal/codec.hpp>
#include <pal/memory.hpp>
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

} // namespace

certificate::impl_type::impl_type (cert_ptr x509, std::span<const std::byte> der)
	: x509{std::move(x509)}
	, bytes_buf{der.size()}
	, bytes{init_bytes(der)}
	, serial_number{init_serial_number()}
	, common_name{init_common_name()}
	, fingerprint{init_fingerprint()}
	, not_before{to_time(::X509_get_notBefore(this->x509.get()))}
	, not_after{to_time(::X509_get_notAfter(this->x509.get()))}
{
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
		return pal::make_shared<impl_type>(std::move(x509), der).transform(impl_type::to_api);
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

} // namespace pal::crypto

#endif // __pal_crypto_openssl
