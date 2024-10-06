// -*- C++ -*-

#include <pal/crypto/hash>
#include <pal/crypto/certificate_store>
#include <pal/net/ip/address_v6>
#include <CoreFoundation/CFNumber.h>
#include <CoreFoundation/CFString.h>
#include <CoreFoundation/CFURL.h>
#include <Security/SecCertificate.h>
#include <Security/SecCertificateOIDs.h>
#include <Security/SecIdentity.h>
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

size_t copy_string (::CFTypeRef s, char *first, char *last) noexcept
{
	*first = *(last - 1) = '\0';
	::CFStringGetCString((::CFStringRef)s, first, last - first, cstr_encoding);
	return ::CFStringGetLength((::CFStringRef)s);
}

size_t copy_uri (::CFTypeRef u, char *first, char *last) noexcept
{
	return copy_string(::CFURLGetString((::CFURLRef)u), first, last);
}

size_t copy_ip (::CFTypeRef s, char *first, char *last) noexcept
{
	static constexpr auto v6_length = sizeof("0000:0000:0000:0000:0000:0000:0000:0000") - 1;

	auto length = copy_string(s, first, last);
	if (length == v6_length)
	{
		net::ip::make_address_v6(first).and_then([&](const auto &address)
		{
			length = address.to_chars(first, last).ptr - first;
		});
	}

	return length;
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

	unique_ref<::SecKeyRef> private_key = make_unique((::SecKeyRef)nullptr);

	impl_ptr next = nullptr;

	impl_type (const impl_type &) = delete;
	impl_type &operator= (const impl_type &) = delete;

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

	static result<impl_ptr> make_forward_list (::CFArrayRef chain) noexcept
	{
		auto index = ::CFArrayGetCount(chain);
		if (!index)
		{
			return {};
		}

		auto chain_at = [&chain](::CFIndex index)
		{
			return make_unique(
				(::SecCertificateRef)::CFRetain(
					::CFArrayGetValueAtIndex(chain, index)
				)
			);
		};

		return pal::make_shared<impl_type>(chain_at(0))
			.and_then([&](impl_ptr &&head) -> result<impl_ptr>
			{
				for (auto tail = head; index > 1; tail = tail->next)
				{
					if (auto node = pal::make_shared<impl_type>(chain_at(--index)))
					{
						tail->next = std::move(*node);
					}
					else
					{
						return unexpected{node.error()};
					}
				}
				return head;
			})
		;
	}

	static constexpr auto to_api = [](impl_ptr &&value) -> certificate
	{
		return {std::move(value)};
	};
};

result<certificate> certificate::import_der (const std::span<const std::byte> &der) noexcept
{
	auto data = from_span(der);
	if (auto x509 = make_unique(::SecCertificateCreateWithData(nullptr, data.get())))
	{
		return pal::make_shared<impl_type>(std::move(x509)).transform(impl_type::to_api);
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

alternative_name_value certificate::subject_alternative_name_value () const noexcept
{
	auto info = copy_values(impl_->x509.get(), ::kSecOIDSubjectAltName);
	if (!info)
	{
		return {};
	}

	alternative_name_value result;
	auto *p = result.data_, * const end = p + sizeof(result.data_);
	size_t index_size = 0;

	auto count = ::CFArrayGetCount(info.get());
	for (auto i = 0; i < count && index_size < result.max_index_size && p < end; ++i)
	{
		auto entry = (::CFDictionaryRef)::CFArrayGetValueAtIndex(info.get(), i);

		auto key = ::CFDictionaryGetValue(entry, ::kSecPropertyKeyLabel);
		auto value = ::CFDictionaryGetValue(entry, ::kSecPropertyKeyValue);
		size_t value_size = 0;

		if (::CFEqual(key, CFSTR("DNS Name")))
		{
			value_size = copy_string(value, p, end);
		}
		else if (::CFEqual(key, CFSTR("Email Address")))
		{
			value_size = copy_string(value, p, end);
		}
		else if (::CFEqual(key, CFSTR("IP Address")))
		{
			value_size = copy_ip(value, p, end);
		}
		else if (::CFEqual(key, CFSTR("URI")))
		{
			value_size = copy_uri(value, p, end);
		}

		if (value_size)
		{
			result.index_data_[index_size++] = {p, value_size};
			p += value_size;
		}
	}

	if (index_size)
	{
		result.index_ = {result.index_data_.data(), index_size};
	}

	return result;
}

// certificate_store / pkcs12 {{{1

struct certificate_store::impl_type
{
	certificate::impl_ptr head;

	impl_type (const certificate::impl_ptr &head) noexcept
		: head{head}
	{ }

	static constexpr auto to_api = [](impl_ptr &&value) -> certificate_store
	{
		return {std::move(value)};
	};
};

bool certificate_store::empty () const noexcept
{
	return !impl_ || impl_->head == nullptr;
}

namespace {

unique_ref<::CFDictionaryRef> pkcs12_import_options (const unique_ref<::CFStringRef> &passphrase) noexcept
{
	const void
		*keys[] =
		{
			::kSecImportExportPassphrase,
			::kSecUseDataProtectionKeychain,
		},
		*values[] =
		{
			passphrase.get(),
			&::kCFBooleanTrue,
		};
	const auto count = sizeof(keys)/sizeof(keys[0]);
	return make_unique(::CFDictionaryCreate(nullptr, keys, values, count, nullptr, nullptr));
}

} // namespace

result<certificate_store> certificate_store::import_pkcs12 (const std::span<const std::byte> &pkcs12, const char *password) noexcept
{
	auto blob = from_span(pkcs12);
	auto passphrase = from_cstr(password ? password : "");
	auto options = pkcs12_import_options(passphrase);

	::CFArrayRef items = nullptr;
	if (::SecPKCS12Import(blob.get(), options.get(), &items) != ::errSecSuccess)
	{
		return make_unexpected(std::errc::invalid_argument);
	}

	auto item_guard = make_unique(items);
	auto data = (::CFDictionaryRef)::CFArrayGetValueAtIndex(items, 0);
	auto chain = (::CFArrayRef)::CFDictionaryGetValue(data, ::kSecImportItemCertChain);
	auto identity = (::SecIdentityRef)::CFDictionaryGetValue(data, ::kSecImportItemIdentity);

	auto attach_private_key = [&identity](certificate::impl_ptr &&head) -> result<certificate::impl_ptr>
	{
		if (identity)
		{
			::SecKeyRef pkey;
			if (::SecIdentityCopyPrivateKey(identity, &pkey) != ::errSecSuccess)
			{
				return make_unexpected(std::errc::invalid_argument);
			}
			head->private_key.reset(pkey);
		}
		return head;
	};

	return certificate::impl_type::make_forward_list(chain)
		.and_then(attach_private_key)
		.and_then(pal::make_shared<impl_type, certificate::impl_ptr>)
		.transform(impl_type::to_api)
	;
}

certificate_store::const_iterator::const_iterator (const impl_type &store) noexcept
	: entry_{store.head}
{ }

certificate_store::const_iterator &certificate_store::const_iterator::operator++ () noexcept
{
	entry_.impl_ = entry_.impl_->next;
	return *this;
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

	static constexpr auto to_api = [](impl_ptr &&value) -> distinguished_name
	{
		return {std::move(value)};
	};

	static result<distinguished_name> make (certificate::impl_ptr owner, ::CFTypeRef id) noexcept
	{
		if (auto name = copy_values(owner->x509.get(), id))
		{
			return pal::make_shared<impl_type>(owner, std::move(name)).transform(to_api);
		}
		return distinguished_name{nullptr};
	}
};

void distinguished_name::const_iterator::load_next_entry () noexcept
{
	if (at_ < owner_->size)
	{
		auto entry = (::CFDictionaryRef)::CFArrayGetValueAtIndex(owner_->name.get(), at_++);

		copy_string(::CFDictionaryGetValue(entry, ::kSecPropertyKeyLabel), oid_, oid_ + sizeof(oid_));
		entry_.oid = oid_;

		copy_string(::CFDictionaryGetValue(entry, ::kSecPropertyKeyValue), value_, value_ + sizeof(value_));
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

	static constexpr auto to_api = [](impl_ptr &&value) -> alternative_name
	{
		return {std::move(value)};
	};

	static result<alternative_name> make (certificate::impl_ptr owner, ::CFTypeRef id) noexcept
	{
		if (auto name = copy_values(owner->x509.get(), id))
		{
			return pal::make_shared<impl_type>(owner, std::move(name)).transform(to_api);
		}
		return alternative_name{nullptr};
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
			copy_string(value, entry_value_, entry_value_ + sizeof(entry_value_));
			entry_.emplace<dns_name>(entry_value_);
			return;
		}
		else if (::CFEqual(label, CFSTR("Email Address")))
		{
			copy_string(value, entry_value_, entry_value_ + sizeof(entry_value_));
			entry_.emplace<email_address>(entry_value_);
			return;
		}
		else if (::CFEqual(label, CFSTR("IP Address")))
		{
			copy_ip(value, entry_value_, entry_value_ + sizeof(entry_value_));
			entry_.emplace<ip_address>(entry_value_);
			return;
		}
		else if (::CFEqual(label, CFSTR("URI")))
		{
			copy_uri(value, entry_value_, entry_value_ + sizeof(entry_value_));
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

	struct key_properties
	{
		key_algorithm algorithm;
		size_t size_bits, max_block_size;
	};
	const key_properties properties;

	impl_type (certificate::impl_ptr owner, unique_ref<::SecKeyRef> pkey) noexcept
		: owner{owner}
		, pkey{std::move(pkey)}
		, properties{get_key_properties(this->pkey.get())}
	{ }

	static constexpr auto to_api = [](impl_ptr &&value) -> key
	{
		return {std::move(value)};
	};

	static result<key> make (certificate::impl_ptr owner, unique_ref<::SecKeyRef> pkey) noexcept
	{
		return pal::make_shared<impl_type>(owner, std::move(pkey)).transform(to_api);
	}

	static key_properties get_key_properties (::SecKeyRef pkey) noexcept
	{
		key_properties result
		{
			.algorithm = key_algorithm::opaque,
			.size_bits = 0,
			.max_block_size = ::SecKeyGetBlockSize(pkey),
		};

		auto dir = make_unique(::SecKeyCopyAttributes(pkey));
		::CFTypeRef v;

		if (::CFDictionaryGetValueIfPresent(dir.get(), ::kSecAttrKeyType, &v))
		{
			if (::CFEqual(v, ::kSecAttrKeyTypeRSA))
			{
				result.algorithm = key_algorithm::rsa;
			}
		}

		if (::CFDictionaryGetValueIfPresent(dir.get(), ::kSecAttrKeySizeInBits, &v))
		{
			::CFNumberGetValue(static_cast<::CFNumberRef>(v), kCFNumberSInt64Type, &result.size_bits);
		}

		return result;
	}
};

key_algorithm key::algorithm () const noexcept
{
	return impl_->properties.algorithm;
}

size_t key::size_bits () const noexcept
{
	return impl_->properties.size_bits;
}

size_t key::max_block_size () const noexcept
{
	return impl_->properties.max_block_size;
}

result<key> certificate::public_key () const noexcept
{
	return key::impl_type::make(impl_, make_unique(::SecCertificateCopyKey(impl_->x509.get())));
}

result<key> certificate::private_key () const noexcept
{
	if (auto pkey = impl_->private_key.get())
	{
		::CFRetain(pkey);
		return key::impl_type::make(impl_, make_unique(pkey));
	}
	return make_unexpected(std::errc::io_error);
}

//}}}1

} // namespace pal::crypto
