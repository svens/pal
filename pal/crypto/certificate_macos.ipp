#include <pal/crypto/hash>
#include <CoreFoundation/CFNumber.h>
#include <CoreFoundation/CFString.h>
#include <Security/SecCertificate.h>
#include <Security/SecCertificateOIDs.h>

namespace pal::crypto {

namespace {

unique_ref<::CFArrayRef> copy_values (::SecCertificateRef cert, ::CFTypeRef oid) noexcept
{
	auto keys = make_unique(::CFArrayCreate(nullptr, &oid, 1, &kCFTypeArrayCallBacks));
	auto dir = make_unique(::SecCertificateCopyValues(cert, keys.get(), nullptr));

	::CFTypeRef values{};
	if (::CFDictionaryGetValueIfPresent(dir.get(), oid, &values))
	{
		if (::CFDictionaryGetValueIfPresent((::CFDictionaryRef)values, ::kSecPropertyKeyValue, &values))
		{
			::CFRetain(values);
		}
		else
		{
			values = nullptr;
		}
	}
	return make_unique((::CFArrayRef)values);
}

certificate::time_type to_time (::SecCertificateRef cert, ::CFTypeRef oid) noexcept
{
	::CFAbsoluteTime time{};
	if (auto value = copy_values(cert, oid))
	{
		::CFNumberGetValue((::CFNumberRef)value.get(), ::kCFNumberDoubleType, &time);
		time += ::kCFAbsoluteTimeIntervalSince1970;
	}
	return certificate::clock_type::from_time_t(time);
}

} // namespace

struct certificate::impl_type
{
	using x509_ptr = unique_ref<::SecCertificateRef>;
	x509_ptr x509;

	char common_name_buf[512 + 1];
	std::string_view common_name{};

	char fingerprint_buf[hex::encode_size(sha1_hash::digest_size) + 1];
	std::string_view fingerprint{};

	time_type not_before, not_after;

	impl_type (x509_ptr &&x509, const std::span<const std::byte> &der) noexcept
		: x509{std::move(x509)}
		, common_name{init_common_name()}
		, fingerprint{init_fingerprint(der)}
		, not_before{to_time(this->x509.get(), ::kSecOIDX509V1ValidityNotBefore)}
		, not_after{to_time(this->x509.get(), ::kSecOIDX509V1ValidityNotAfter)}
	{ }

	std::string_view init_common_name () noexcept
	{
		::CFStringRef p = nullptr; // TODO: std::out_ptr
		if (::SecCertificateCopyCommonName(x509.get(), &p) == ::errSecSuccess)
		{
			auto cn = make_unique(p);
			::CFStringGetCString(
				cn.get(),
				common_name_buf,
				sizeof(common_name_buf),
				::kCFStringEncodingUTF8
			);
			return common_name_buf;
		}
		return "";
	}

	std::string_view init_fingerprint (const std::span<const std::byte> &der) noexcept
	{
		encode<hex>(*sha1_hash::one_shot(der), fingerprint_buf);
		fingerprint_buf[sizeof(fingerprint_buf) - 1] = '\0';
		return fingerprint_buf;
	}
};

result<certificate> certificate::import_der (std::span<const std::byte> der) noexcept
{
	auto data = make_unique(
		::CFDataCreateWithBytesNoCopy(
			nullptr,
			reinterpret_cast<const UInt8 *>(der.data()),
			der.size(),
			kCFAllocatorNull
		)
	);

	if (auto x509 = make_unique(::SecCertificateCreateWithData(nullptr, data.get())))
	{
		if (auto impl = impl_ptr{new(std::nothrow) impl_type(std::move(x509), der)})
		{
			return certificate{impl};
		}
		return make_unexpected(std::errc::not_enough_memory);
	}

	return make_unexpected(std::errc::invalid_argument);
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

} // namespace pal::crypto