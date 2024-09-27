// -*- C++ -*-

#include <pal/crypto/hash>
#include <pal/crypto/certificate_store>
#include <pal/net/ip/address_v6>
#include <CoreFoundation/CFNumber.h>
#include <CoreFoundation/CFString.h>
#include <CoreFoundation/CFURL.h>
#include <Security/SecCertificate.h>
#include <Security/SecCertificateOIDs.h>
#include <Security/SecImportExport.h>
#include <Security/SecItem.h>
#include <Security/SecKey.h>

namespace pal::crypto {

namespace {

static constexpr auto cstr_encoding = ::kCFStringEncodingUTF8;

template <size_t N>
const char *c_str (::CFTypeRef s, char (&buf)[N]) noexcept
{
	if (auto p = ::CFStringGetCStringPtr((::CFStringRef)s, cstr_encoding))
	{
		return p;
	}

	::CFStringGetCString((::CFStringRef)s, buf, N, cstr_encoding);
	return static_cast<const char *>(&buf[0]);
}

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

std::span<const std::byte> to_span (::CFDataRef data) noexcept
{
	return
	{
		reinterpret_cast<const std::byte *>(::CFDataGetBytePtr(data)),
		static_cast<size_t>(::CFDataGetLength(data))
	};
}

unique_ref<::CFDataRef> from_span (const std::span<const std::byte> &data) noexcept
{
	return make_unique(
		::CFDataCreateWithBytesNoCopy(
			nullptr,
			reinterpret_cast<const UInt8 *>(data.data()),
			data.size(),
			::kCFAllocatorNull
		)
	);
}

unique_ref<::CFStringRef> from_cstr (const char *data) noexcept
{
	return make_unique(::CFStringCreateWithCStringNoCopy(nullptr, data, cstr_encoding, ::kCFAllocatorNull));
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

// certificate {{{1

struct certificate::impl_type
{
	using x509_ptr = unique_ref<::SecCertificateRef>;
	x509_ptr x509;

	unique_ref<::CFDataRef> bytes_buf;
	std::span<const std::byte> bytes;

	int version;

	std::array<uint8_t, 20> serial_number_buf{};
	std::span<const uint8_t> serial_number;

	char common_name_buf[64];
	std::string_view common_name{};

	char fingerprint_buf[hex::encode_size(sha1_hash::digest_size) + 1];
	std::string_view fingerprint{};

	time_type not_before, not_after;

	impl_type (x509_ptr &&x509) noexcept
		: x509{std::move(x509)}
		, bytes_buf{make_unique(::SecCertificateCopyData(this->x509.get()))}
		, bytes{to_span(this->bytes_buf.get())}
		, version{init_version()}
		, serial_number{init_serial_number()}
		, common_name{init_common_name()}
		, fingerprint{init_fingerprint()}
		, not_before{to_time(this->x509.get(), ::kSecOIDX509V1ValidityNotBefore)}
		, not_after{to_time(this->x509.get(), ::kSecOIDX509V1ValidityNotAfter)}
	{ }

	int init_version () noexcept
	{
		char buf[16] = "0";
		const char *p = buf;
		if (auto value = copy_values(x509.get(), ::kSecOIDX509V1Version))
		{
			p = c_str(value.get(), buf);
		}
		return std::atoi(p);
	}

	std::span<const uint8_t> init_serial_number () noexcept
	{
		auto value = make_unique(::SecCertificateCopySerialNumberData(x509.get(), nullptr));

		auto first = ::CFDataGetBytePtr(value.get());
		auto last = first + ::CFDataGetLength(value.get());
		first = std::find_if_not(first, last, [](auto &v){ return v == 0; });

		return
		{
			serial_number_buf.begin(),
			std::copy(first, last, serial_number_buf.begin())
		};
	}

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
				cstr_encoding
			);
			return common_name_buf;
		}
		return "";
	}

	std::string_view init_fingerprint () noexcept
	{
		encode<hex>(*sha1_hash::one_shot(bytes), fingerprint_buf);
		fingerprint_buf[sizeof(fingerprint_buf) - 1] = '\0';
		return fingerprint_buf;
	}
};

result<certificate> certificate::import_der (const std::span<const std::byte> &der) noexcept
{
	auto data = from_span(der);
	if (auto x509 = make_unique(::SecCertificateCreateWithData(nullptr, data.get())))
	{
		if (auto impl = impl_ptr{new(std::nothrow) impl_type(std::move(x509))})
		{
			return certificate{impl};
		}
		return make_unexpected(std::errc::not_enough_memory);
	}

	return make_unexpected(std::errc::invalid_argument);
}

std::span<const std::byte> certificate::as_bytes () const noexcept
{
	return impl_->bytes;
}

int certificate::version () const noexcept
{
	return impl_->version;
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
	auto this_issuer = make_unique(::SecCertificateCopyNormalizedIssuerSequence(impl_->x509.get()));
	auto that_subject = make_unique(::SecCertificateCopyNormalizedSubjectSequence(that.impl_->x509.get()));

	auto this_issuer_size = ::CFDataGetLength(this_issuer.get());
	auto that_subject_size = ::CFDataGetLength(that_subject.get());
	if (this_issuer_size != that_subject_size)
	{
		return false;
	}

	auto this_issuer_data = ::CFDataGetBytePtr(this_issuer.get());
	auto that_subject_data = ::CFDataGetBytePtr(that_subject.get());
	return std::equal(this_issuer_data, this_issuer_data + this_issuer_size, that_subject_data);
}

// certificate_store / pkcs12 {{{1

result<certificate_store> certificate_store::import_pkcs12 (const std::span<const std::byte> &pkcs12, const char *password) noexcept
{
	struct pkcs12_store: public impl_api
	{
		unique_ref<::CFArrayRef> chain;
		const size_t chain_size;

		pkcs12_store (unique_ref<::CFArrayRef> &&chain, size_t chain_size) noexcept
			: chain{std::move(chain)}
			, chain_size{chain_size}
		{ }

		~pkcs12_store () noexcept override = default;

		size_t size () const noexcept override
		{
			return chain_size;
		}

		result<certificate::impl_ptr> at (size_t index) const noexcept override
		{
			if (index >= chain_size)
			{
				return make_unexpected(std::errc::invalid_argument);
			}

			auto cert = make_unique((::SecCertificateRef)::CFArrayGetValueAtIndex(chain.get(), index));
			::CFRetain(cert.get());
			if (auto result = certificate::impl_ptr{new(std::nothrow) certificate::impl_type(std::move(cert))})
			{
				// TODO: attach private_key in identity
				return result;
			}

			return make_unexpected(std::errc::not_enough_memory);
		}
	};

	auto blob = from_span(pkcs12);
	auto passphrase = from_cstr(password ? password : "");

	::CFStringRef extension = nullptr;
	::SecExternalFormat format = ::kSecFormatPKCS12;
	::SecExternalItemType type = ::kSecItemTypeAggregate;
	::SecItemImportExportFlags flags = 0;
	::SecItemImportExportKeyParameters params =
	{
		.version = SEC_KEY_IMPORT_EXPORT_PARAMS_VERSION,
		.flags = ::kSecKeyImportOnlyOne,
		.passphrase = passphrase.get(),
		.alertTitle = nullptr,
		.alertPrompt = nullptr,
		.accessRef = nullptr,
		.keyUsage = nullptr,
		.keyAttributes = nullptr,
	};
	::SecKeychainRef keychain = nullptr;
	::CFArrayRef rv = nullptr;

	auto status = ::SecItemImport(
		blob.get(),
		extension,
		&format,
		&type,
		flags,
		&params,
		keychain,
		&rv
	);

	if (status != ::errSecSuccess)
	{
		return make_unexpected(std::errc::invalid_argument);
	}

	auto chain = make_unique(rv);
	auto chain_size = ::CFArrayGetCount(chain.get());
	if (chain_size < 1)
	{
		return make_unexpected(std::errc::invalid_argument);
	}

	if (auto impl = impl_ptr{new(std::nothrow) pkcs12_store(std::move(chain), chain_size)})
	{
		return certificate_store{std::move(impl)};
	}

	return make_unexpected(std::errc::not_enough_memory);
}

// distinguished_name {{{1

struct distinguished_name::impl_type
{
	certificate::impl_ptr owner;
	unique_ref<::CFArrayRef> name;
	const size_t size;

	impl_type (certificate::impl_ptr owner, unique_ref<::CFArrayRef> &&name) noexcept
		: owner{owner}
		, name{std::move(name)}
		, size{static_cast<size_t>(::CFArrayGetCount(this->name.get()))}
	{ }

	static result<distinguished_name> make (certificate::impl_ptr owner, ::CFTypeRef id) noexcept
	{
		impl_ptr list = nullptr;

		if (auto name = copy_values(owner->x509.get(), id))
		{
			list.reset(new(std::nothrow) impl_type(owner, std::move(name)));
			if (!list)
			{
				return make_unexpected(std::errc::not_enough_memory);
			}
		}

		return distinguished_name{list};
	}
};

namespace {

template <size_t N>
void copy_string (::CFTypeRef s, char (&buf)[N]) noexcept
{
	buf[0] = buf[N - 1] = '\0';
	::CFStringGetCString((::CFStringRef)s, buf, N, cstr_encoding);
}

template <size_t N>
void copy_uri (::CFTypeRef u, char (&buf)[N]) noexcept
{
	copy_string(::CFURLGetString((::CFURLRef)u), buf);
}

template <size_t N>
void copy_ip (::CFTypeRef s, char (&buf)[N]) noexcept
{
	copy_string(s, buf);

	static constexpr auto v6_length = sizeof("0000:0000:0000:0000:0000:0000:0000:0000") - 1;
	auto length = ::CFStringGetLength((::CFStringRef)s);
	if (length == v6_length)
	{
		net::ip::make_address_v6(buf).and_then([&](const auto &address)
		{
			address.to_chars(buf, buf + sizeof(buf));
		});
	}
}

} // namespace

void distinguished_name::const_iterator::load_next_entry () noexcept
{
	if (at_ < owner_->size)
	{
		auto entry = (::CFDictionaryRef)::CFArrayGetValueAtIndex(owner_->name.get(), at_++);

		copy_string(::CFDictionaryGetValue(entry, ::kSecPropertyKeyLabel), oid_);
		entry_.oid = oid_;

		copy_string(::CFDictionaryGetValue(entry, ::kSecPropertyKeyValue), value_);
		entry_.value = value_;

		return;
	};

	owner_ = nullptr;
}

result<distinguished_name> certificate::issuer_name () const noexcept
{
	return distinguished_name::impl_type::make(impl_, ::kSecOIDX509V1IssuerName);
}

result<distinguished_name> certificate::subject_name () const noexcept
{
	return distinguished_name::impl_type::make(impl_, ::kSecOIDX509V1SubjectName);
}

// alternative_name {{{1

struct alternative_name::impl_type
{
	certificate::impl_ptr owner;
	unique_ref<::CFArrayRef> name;
	const size_t size;

	impl_type (certificate::impl_ptr owner, unique_ref<::CFArrayRef> &&name) noexcept
		: owner{owner}
		, name{std::move(name)}
		, size{static_cast<size_t>(::CFArrayGetCount(this->name.get()))}
	{ }

	static result<alternative_name> make (certificate::impl_ptr owner, ::CFTypeRef id) noexcept
	{
		impl_ptr list = nullptr;

		if (auto name = copy_values(owner->x509.get(), id))
		{
			list.reset(new(std::nothrow) impl_type(owner, std::move(name)));
			if (!list)
			{
				return make_unexpected(std::errc::not_enough_memory);
			}
		}

		return alternative_name{list};
	}
};

void alternative_name::const_iterator::load_next_entry () noexcept
{
	if (!owner_)
	{
		return;
	}

	while (at_ < owner_->size)
	{
		auto entry = (::CFDictionaryRef)::CFArrayGetValueAtIndex(owner_->name.get(), at_++);
		auto label = ::CFDictionaryGetValue(entry, ::kSecPropertyKeyLabel);
		auto value = ::CFDictionaryGetValue(entry, ::kSecPropertyKeyValue);

		if (::CFEqual(label, CFSTR("DNS Name")))
		{
			copy_string(value, entry_value_);
			entry_.emplace<dns_name>(entry_value_);
			return;
		}
		else if (::CFEqual(label, CFSTR("Email Address")))
		{
			copy_string(value, entry_value_);
			entry_.emplace<email_address>(entry_value_);
			return;
		}
		else if (::CFEqual(label, CFSTR("IP Address")))
		{
			copy_ip(value, entry_value_);
			entry_.emplace<ip_address>(entry_value_);
			return;
		}
		else if (::CFEqual(label, CFSTR("URI")))
		{
			copy_uri(value, entry_value_);
			entry_.emplace<uri>(entry_value_);
			return;
		}
	}

	owner_ = nullptr;
	entry_.emplace<std::monostate>();
}

result<alternative_name> certificate::issuer_alternative_name () const noexcept
{
	return alternative_name::impl_type::make(impl_, ::kSecOIDIssuerAltName);
}

result<alternative_name> certificate::subject_alternative_name () const noexcept
{
	return alternative_name::impl_type::make(impl_, ::kSecOIDSubjectAltName);
}

// key {{{1

struct key::impl_type
{
	certificate::impl_ptr owner;
	unique_ref<::SecKeyRef> pkey;
	const size_t size_bits, max_block_size;

	impl_type (certificate::impl_ptr owner, unique_ref<::SecKeyRef> pkey) noexcept
		: owner{owner}
		, pkey{std::move(pkey)}
		, size_bits{get_size_bits(this->pkey.get())}
		, max_block_size{::SecKeyGetBlockSize(this->pkey.get())}
	{ }

	static result<key> make (certificate::impl_ptr owner, unique_ref<::SecKeyRef> pkey) noexcept
	{
		if (auto impl = impl_ptr{new(std::nothrow) impl_type(owner, std::move(pkey))})
		{
			return key{impl};
		}
		return make_unexpected(std::errc::not_enough_memory);
	}

	static size_t get_size_bits (::SecKeyRef pkey) noexcept
	{
		size_t result = 0;

		::CFTypeRef v;
		auto dir = make_unique(::SecKeyCopyAttributes(pkey));
		if (::CFDictionaryGetValueIfPresent(dir.get(), ::kSecAttrKeySizeInBits, &v))
		{
			::CFNumberGetValue(static_cast<::CFNumberRef>(v), kCFNumberSInt64Type, &result);
		}

		return result;
	}
};

size_t key::size_bits () const noexcept
{
	return impl_->size_bits;
}

size_t key::max_block_size () const noexcept
{
	return impl_->max_block_size;
}

result<key> certificate::public_key () const noexcept
{
	return key::impl_type::make(impl_, make_unique(::SecCertificateCopyKey(impl_->x509.get())));
}

//}}}1

} // namespace pal::crypto
