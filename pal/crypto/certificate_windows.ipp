// -*- C++ -*-

#include <pal/crypto/hash>
#include <pal/crypto/certificate_store>
#include <pal/error>
#include <pal/net/ip/address_v4>
#include <pal/net/ip/address_v6>
#include <pal/memory>
#include <wincrypt.h>
#include <algorithm>
#include <deque>

namespace pal::crypto {

namespace {

certificate::time_type to_time (const FILETIME &time) noexcept
{
	std::tm tm{};
	SYSTEMTIME sys_time;
	if (::FileTimeToSystemTime(&time, &sys_time))
	{
		tm.tm_sec = sys_time.wSecond;
		tm.tm_min = sys_time.wMinute;
		tm.tm_hour = sys_time.wHour;
		tm.tm_mday = sys_time.wDay;
		tm.tm_mon = sys_time.wMonth - 1;
		tm.tm_year = sys_time.wYear - 1900;
	}
	return certificate::clock_type::from_time_t(std::mktime(&tm));
}

size_t copy_oid (const CERT_RDN_ATTR &entry, char *first, char *last) noexcept
{
	*first = *(last - 1) = '\0';
	return std::snprintf(first, last - first, "%s", entry.pszObjId);
}

size_t copy_string (const LPWSTR &string, char *first, char *last) noexcept
{
	*first = *(last - 1) = '\0';
	auto size = ::WideCharToMultiByte(
		CP_UTF8,
		0,
		string,
		-1,
		first,
		static_cast<int>(last - first),
		nullptr,
		nullptr
	);
	return size > 0 ? size - 1 : 0;
}

size_t copy_ip (const void *data, size_t length, char *first, char *last) noexcept
{
	*first = '\0';
	if (length == sizeof(net::ip::address_v4::bytes_type))
	{
		auto &bytes = *static_cast<const net::ip::address_v4::bytes_type *>(data);
		return net::ip::make_address_v4(bytes).to_chars(first, last).ptr - first;
	}
	else if (length == sizeof(net::ip::address_v6::bytes_type))
	{
		auto &bytes = *static_cast<const net::ip::address_v6::bytes_type *>(data);
		return net::ip::make_address_v6(bytes).to_chars(first, last).ptr - first;
	}
	return 0;
}

size_t copy_rdn_value (const CERT_RDN_ATTR &entry, char *first, char *last) noexcept
{
	auto size = ::CertRDNValueToStr(
		entry.dwValueType,
		const_cast<CERT_RDN_VALUE_BLOB *>(&entry.Value),
		first,
		static_cast<DWORD>(last - first)
	);
	return size > 0 ? size - 1 : 0;
}

template <typename T, size_t Size>
struct asn_decoder
{
	LPCSTR type;
	std::span<const BYTE> asn;
	const DWORD buffer_size;
	temporary_buffer<Size> buffer;
	const T &value;

	asn_decoder (LPCSTR type, std::span<const BYTE> asn) noexcept
		: type{type}
		, asn{asn}
		, buffer_size{decode(nullptr, 0)}
		, buffer{std::nothrow, buffer_size}
		, value{*reinterpret_cast<const T *>(buffer.get())}
	{
		decode(buffer.get(), buffer_size);
	}

	DWORD decode (void *buf, DWORD buf_size) const noexcept
	{
		static constexpr DWORD decode_flags =
			CRYPT_DECODE_NOCOPY_FLAG |
			CRYPT_DECODE_SHARE_OID_STRING_FLAG
		;

		DWORD size = buf_size;
		::CryptDecodeObjectEx(
			X509_ASN_ENCODING,
			type,
			asn.data(),
			static_cast<DWORD>(asn.size()),
			decode_flags,
			nullptr,
			buf,
			&size
		);
		return size;
	}
};

// X509 {{{1

using x509_ptr = std::unique_ptr<const ::CERT_CONTEXT, decltype(&::CertFreeCertificateContext)>;

x509_ptr to_ptr (PCCERT_CONTEXT p) noexcept
{
	return {p, &::CertFreeCertificateContext};
}

// X509 system store {{{1

using store_handle = uintptr_t;
static_assert(sizeof(store_handle) == sizeof(::HCERTSTORE));

void close_store (store_handle *store) noexcept
{
	::CertCloseStore(reinterpret_cast<::HCERTSTORE>(store), 0);
}

using store_ptr = std::unique_ptr<store_handle, decltype(&close_store)>;

store_ptr to_ptr (::HCERTSTORE store) noexcept
{
	return {reinterpret_cast<store_handle *>(store), &close_store};
}

// }}}1

} // namespace

// certificate {{{1

struct certificate::impl_type
{
	x509_ptr x509;

	std::array<uint8_t, 20> serial_number_buf{};
	std::span<const uint8_t> serial_number;

	char common_name_buf[64];
	std::string_view common_name;

	char fingerprint_buf[hex::encode_size(sha1_hash::digest_size) + 1];
	std::string_view fingerprint;

	time_type not_before, not_after;

	impl_ptr next = nullptr;

	impl_type (const impl_type &) = delete;
	impl_type &operator= (const impl_type &) = delete;

	impl_type (x509_ptr &&x509) noexcept
		: x509{std::move(x509)}
		, serial_number{init_serial_number()}
		, common_name{init_common_name()}
		, fingerprint{init_fingerprint()}
		, not_before{to_time(this->x509->pCertInfo->NotBefore)}
		, not_after{to_time(this->x509->pCertInfo->NotAfter)}
	{ }

	std::span<const uint8_t> init_serial_number () noexcept
	{
		auto first = x509->pCertInfo->SerialNumber.pbData;
		auto last = first + x509->pCertInfo->SerialNumber.cbData;

		while (last != first && !last[-1])
		{
			--last;
		}

		return
		{
			serial_number_buf.begin(),
			std::reverse_copy(first, last, serial_number_buf.begin())
		};
	}

	std::string_view init_common_name () noexcept
	{
		static char oid[] = szOID_COMMON_NAME;
		::CertGetNameString(
			x509.get(),
			CERT_NAME_ATTR_TYPE,
			0,
			oid,
			common_name_buf,
			sizeof(common_name_buf)
		);
		return common_name_buf;
	}

	std::string_view init_fingerprint () noexcept
	{
		sha1_hash::digest_type buf;
		auto buf_size = static_cast<DWORD>(buf.size());
		::CertGetCertificateContextProperty(
			x509.get(),
			CERT_SHA1_HASH_PROP_ID,
			buf.data(),
			&buf_size
		);
		encode<hex>(buf, fingerprint_buf);
		fingerprint_buf[sizeof(fingerprint_buf) - 1] = '\0';
		return fingerprint_buf;
	}

	static result<impl_ptr> make_forward_list (store_ptr &&chain) noexcept
	{
		impl_ptr head = nullptr;

		::PCCERT_CONTEXT it = nullptr;
		while ((it = ::CertEnumCertificatesInStore(chain.get(), it)) != nullptr)
		{
			auto x509 = to_ptr(::CertDuplicateCertificateContext(it));
			auto cert = impl_ptr{new(std::nothrow) impl_type(std::move(x509))};
			if (!cert)
			{
				return make_unexpected(std::errc::not_enough_memory);
			}
			cert->next = head;
			head = cert;
		}

		// reorder: keep head (leaf), reverse rest from intermediate toward issuer
		if (head->next)
		{
			auto rest = std::exchange(head->next, nullptr);
			while (rest)
			{
				auto next = rest->next;
				rest->next = head->next;
				head->next = rest;
				rest = next;
			}
		}

		return head;
	}
};

result<certificate> certificate::import_der (const std::span<const std::byte> &der) noexcept
{
	auto x509 = to_ptr(
		::CertCreateCertificateContext(
			X509_ASN_ENCODING,
			reinterpret_cast<const BYTE *>(der.data()),
			static_cast<DWORD>(der.size())
		)
	);

	if (x509)
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
	return
	{
		reinterpret_cast<const std::byte *>(impl_->x509->pbCertEncoded),
		impl_->x509->cbCertEncoded
	};
}

int certificate::version () const noexcept
{
	return impl_->x509->pCertInfo->dwVersion + 1;
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
	return ::CertCompareCertificateName(
		X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
		&that.impl_->x509->pCertInfo->Subject,
		&impl_->x509->pCertInfo->Issuer
	);
}

alternative_name_values certificate::subject_alternative_name_values () const noexcept
{
	auto &info = *impl_->x509->pCertInfo;
	auto ext = ::CertFindExtension(szOID_SUBJECT_ALT_NAME2, info.cExtension, info.rgExtension);
	if (!ext)
	{
		return {};
	}

	asn_decoder<CERT_ALT_NAME_INFO, 4096> name{X509_ALTERNATE_NAME, {ext->Value.pbData, ext->Value.cbData}};
	auto count = name.value.cAltEntry;

	alternative_name_values result{};
	auto *p = result.data_, * const end = p + sizeof(result.data_);
	size_t index_size = 0;

	for (auto i = 0u; i < count && index_size < result.max_index_size && p < end; ++i)
	{
		size_t value_size = 0;

		const auto &entry = name.value.rgAltEntry[i];
		switch (entry.dwAltNameChoice)
		{
			case CERT_ALT_NAME_DNS_NAME:
				value_size = copy_string(entry.pwszDNSName, p, end);
				break;

			case CERT_ALT_NAME_RFC822_NAME:
				value_size = copy_string(entry.pwszRfc822Name, p, end);
				break;

			case CERT_ALT_NAME_IP_ADDRESS:
				value_size = copy_ip(entry.IPAddress.pbData, entry.IPAddress.cbData, p, end);
				break;

			case CERT_ALT_NAME_URL:
				value_size = copy_string(entry.pwszURL, p, end);
				break;
		}

		if (value_size)
		{
			result.index_[index_size++] = {p, value_size};
			p += value_size;
		}
	}

	return result;
}

// certificate_store {{{1

namespace {

store_ptr open_store (const std::span<const std::byte> &pkcs12, const char *password)
{
	::CRYPT_DATA_BLOB pfx;
	pfx.pbData = reinterpret_cast<uint8_t *>(const_cast<std::byte *>(pkcs12.data()));
	pfx.cbData = static_cast<DWORD>(pkcs12.size_bytes());

	int password_strlen = password ? static_cast<int>(std::strlen(password)) : 0;
	auto password_buf_size = ::MultiByteToWideChar(
		CP_UTF8,
		0,
		password,
		password_strlen,
		nullptr,
		0
	);

	temporary_buffer<1024> password_buf{std::nothrow, static_cast<size_t>(password_buf_size)};
	auto password_buf_ptr = static_cast<LPWSTR>(password_buf.get());
	::MultiByteToWideChar(
		CP_UTF8,
		0,
		password,
		password_strlen,
		password_buf_ptr,
		password_buf_size
	);

	auto result = to_ptr(::PFXImportCertStore(&pfx, password_buf_ptr, 0));
	::SecureZeroMemory(password_buf_ptr, password_buf_size);
	return result;
}

} // namespace

struct certificate_store::impl_type
{
	certificate::impl_ptr head;

	impl_type (const certificate::impl_ptr &head) noexcept
		: head{head}
	{ }
};

bool certificate_store::empty () const noexcept
{
	return impl_->head == nullptr;
}

result<certificate_store> certificate_store::import_pkcs12 (const std::span<const std::byte> &pkcs12, const char *password) noexcept
{
	auto chain = open_store(pkcs12, password);
	if (!chain)
	{
		return make_unexpected(std::errc::invalid_argument);
	}

	auto list = certificate::impl_type::make_forward_list(std::move(chain));
	if (!list)
	{
		return unexpected{list.error()};
	}

	auto impl = impl_ptr{new(std::nothrow) impl_type(std::move(*list))};
	if (!impl)
	{
		return make_unexpected(std::errc::not_enough_memory);
	}

	// TODO: attach private key (::CryptAcquireCertificatePrivateKey)

	return certificate_store{std::move(impl)};
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
	asn_decoder<CERT_NAME_INFO, 3 * 1024> name;
	const size_t size;

	impl_type (certificate::impl_ptr owner, const CERT_NAME_BLOB &name) noexcept
		: owner{owner}
		, name{X509_NAME, {name.pbData, name.cbData}}
		, size{this->name.value.cRDN}
	{ }

	static result<distinguished_name> make (certificate::impl_ptr owner, const CERT_NAME_BLOB &name) noexcept
	{
		impl_ptr list{new(std::nothrow) impl_type{owner, name}};
		if (!list || !list->name.buffer.get())
		{
			return make_unexpected(std::errc::not_enough_memory);
		}
		return distinguished_name{list};
	}
};

void distinguished_name::const_iterator::load_next_entry () noexcept
{
	if (at_ < owner_->size)
	{
		auto &entry = owner_->name.value.rgRDN[at_++].rgRDNAttr[0];

		copy_oid(entry, oid_, oid_ + sizeof(oid_));
		entry_.oid = oid_;

		copy_rdn_value(entry, value_, value_ + sizeof(value_));
		entry_.value = value_;

		return;
	}
	owner_ = nullptr;
}

result<distinguished_name> certificate::issuer_name () const noexcept
{
	return distinguished_name::impl_type::make(impl_, impl_->x509->pCertInfo->Issuer);
}

result<distinguished_name> certificate::subject_name () const noexcept
{
	return distinguished_name::impl_type::make(impl_, impl_->x509->pCertInfo->Subject);
}

// alternative_name {{{1

struct alternative_name::impl_type
{
	certificate::impl_ptr owner;
	asn_decoder<CERT_ALT_NAME_INFO, 3 * 1024> name;
	const size_t size;

	impl_type (certificate::impl_ptr owner, const CERT_EXTENSION &ext) noexcept
		: owner{owner}
		, name{X509_ALTERNATE_NAME, {ext.Value.pbData, ext.Value.cbData}}
		, size{this->name.value.cAltEntry}
	{ }

	static result<alternative_name> make (certificate::impl_ptr owner, LPCSTR oid) noexcept
	{
		impl_ptr list = nullptr;

		auto &info = *owner->x509->pCertInfo;
		if (auto ext = ::CertFindExtension(oid, info.cExtension, info.rgExtension))
		{
			list.reset(new(std::nothrow) impl_type(owner, *ext));
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
		const auto &entry = owner_->name.value.rgAltEntry[at_++];
		switch (entry.dwAltNameChoice)
		{
			case CERT_ALT_NAME_DNS_NAME:
				copy_string(entry.pwszDNSName, entry_value_, entry_value_ + sizeof(entry_value_));
				entry_.emplace<dns_name>(entry_value_);
				return;

			case CERT_ALT_NAME_RFC822_NAME:
				copy_string(entry.pwszRfc822Name, entry_value_, entry_value_ + sizeof(entry_value_));
				entry_.emplace<email_address>(entry_value_);
				return;

			case CERT_ALT_NAME_IP_ADDRESS:
				copy_ip(entry.IPAddress.pbData, entry.IPAddress.cbData, entry_value_, entry_value_ + sizeof(entry_value_));
				entry_.emplace<ip_address>(entry_value_);
				return;

			case CERT_ALT_NAME_URL:
				copy_string(entry.pwszURL, entry_value_, entry_value_ + sizeof(entry_value_));
				entry_.emplace<uri>(entry_value_);
				return;
		}
	}

	owner_ = nullptr;
	entry_.emplace<std::monostate>();
}

result<alternative_name> certificate::issuer_alternative_name () const noexcept
{
	return alternative_name::impl_type::make(impl_, szOID_ISSUER_ALT_NAME2);
}

result<alternative_name> certificate::subject_alternative_name () const noexcept
{
	return alternative_name::impl_type::make(impl_, szOID_SUBJECT_ALT_NAME2);
}

// key {{{1

struct key::impl_type
{
	certificate::impl_ptr owner;
	BCRYPT_KEY_HANDLE pkey;
	const size_t size_bits, max_block_size;

	impl_type (certificate::impl_ptr owner, BCRYPT_KEY_HANDLE pkey) noexcept
		: owner{owner}
		, pkey{pkey}
		, size_bits{get_size_property(pkey, BCRYPT_KEY_LENGTH)}
		, max_block_size{get_size_property(pkey, BCRYPT_BLOCK_LENGTH)}
	{ }

	~impl_type () noexcept
	{
		::BCryptDestroyKey(pkey);
	}

	static result<key> make (certificate::impl_ptr owner, BCRYPT_KEY_HANDLE pkey) noexcept
	{
		if (auto impl = impl_ptr{new(std::nothrow) impl_type(owner, pkey)})
		{
			return key{impl};
		}
		::BCryptDestroyKey(pkey);
		return make_unexpected(std::errc::not_enough_memory);
	}

	static size_t get_size_property (BCRYPT_KEY_HANDLE pkey, LPCWSTR property) noexcept
	{
		size_t result = 0;

		DWORD buf;
		ULONG buf_size;
		auto status = ::BCryptGetProperty(pkey,
			property,
			reinterpret_cast<PUCHAR>(&buf),
			sizeof(buf),
			&buf_size,
			0
		);
		if (status == ERROR_SUCCESS)
		{
			result = buf;
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
	::BCRYPT_KEY_HANDLE pkey;
	auto status = ::CryptImportPublicKeyInfoEx2(
		X509_ASN_ENCODING,
		&impl_->x509->pCertInfo->SubjectPublicKeyInfo,
		0,
		nullptr,
		&pkey
	);
	if (status)
	{
		return key::impl_type::make(impl_, pkey);
	}
	return unexpected{this_thread::last_system_error()};
}

//}}}1

} // namespace pal::crypto
