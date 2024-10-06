// -*- C++ -*-

#include <pal/crypto/hash>
#include <pal/crypto/certificate_store>
#include <pal/net/ip/address_v4>
#include <pal/net/ip/address_v6>
#include <pal/memory>
#include <openssl/asn1.h>
#include <openssl/pkcs12.h>
#include <openssl/x509.h>
#include <cstring>

namespace pal::crypto {

namespace {

certificate::time_type to_time (const ::ASN1_TIME *time) noexcept
{
	std::tm tm{};
	::ASN1_TIME_to_tm(time, &tm);
	return certificate::clock_type::from_time_t(std::mktime(&tm));
}

size_t copy_string (const ::ASN1_STRING *s, char *first, char *last) noexcept
{
	*first = *(last - 1) = '\0';

	// in general, ASN1_STRING_get0_data may contain '\0' in content
	// but in this file it is used only to copy SAN values, should be ok
	std::strncat(first, reinterpret_cast<const char *>(::ASN1_STRING_get0_data(s)), last - first - 1);

	return ::ASN1_STRING_length(s);
}

size_t copy_ip (const ::ASN1_OCTET_STRING *s, char *first, char *last) noexcept
{
	*first = '\0';
	if (s->length == sizeof(net::ip::address_v4::bytes_type))
	{
		auto &bytes = *reinterpret_cast<const net::ip::address_v4::bytes_type *>(s->data);
		return net::ip::make_address_v4(bytes).to_chars(first, last).ptr - first;
	}
	else if (s->length == sizeof(net::ip::address_v6::bytes_type))
	{
		auto &bytes = *reinterpret_cast<const net::ip::address_v6::bytes_type *>(s->data);
		return net::ip::make_address_v6(bytes).to_chars(first, last).ptr - first;
	}
	return 0;
}

template <size_t N>
void copy_oid (const ::ASN1_OBJECT *o, char (&buf)[N]) noexcept
{
	::OBJ_obj2txt(buf, N, o, 1);
}

// X509 {{{1

using x509_ptr = std::unique_ptr<::X509, decltype(&::X509_free)>;

x509_ptr to_ptr (::X509 *p) noexcept
{
	return {p, &::X509_free};
}

void x509_stack_release (STACK_OF(X509) *p) noexcept
{
	::sk_X509_pop_free(p, &::X509_free);
}

// STACK_OF(X509) {{{1

using x509_stack_ptr = std::unique_ptr<STACK_OF(X509), decltype(&x509_stack_release)>;

x509_stack_ptr to_ptr (STACK_OF(X509) *p) noexcept
{
	return {p, &x509_stack_release};
}

// PKEY {{{1

using pkey_ptr = std::unique_ptr<::EVP_PKEY, decltype(&::EVP_PKEY_free)>;

pkey_ptr to_ptr (::EVP_PKEY *p) noexcept
{
	return {p, &::EVP_PKEY_free};
}

// GENERAL_NAMES {{{1

void name_list_release (::GENERAL_NAMES *p) noexcept
{
	::sk_GENERAL_NAME_pop_free(p, ::GENERAL_NAME_free);
}

using name_list_ptr = std::unique_ptr<::GENERAL_NAMES, decltype(&name_list_release)>;

name_list_ptr to_ptr (::GENERAL_NAMES *p) noexcept
{
	return {p, &name_list_release};
}

// }}}1

} // namespace

// certificate {{{1

struct certificate::impl_type
{
	x509_ptr x509;

	temporary_buffer<2048> bytes_buf;
	std::span<const std::byte> bytes;

	std::span<const uint8_t> serial_number;

	std::string_view common_name;

	char fingerprint_buf[hex::encode_size(sha1_hash::digest_size) + 1];
	std::string_view fingerprint;

	time_type not_before, not_after;

	pkey_ptr private_key = to_ptr(static_cast<::EVP_PKEY *>(nullptr));

	impl_ptr next = nullptr;

	impl_type (const impl_type &) = delete;
	impl_type &operator= (const impl_type &) = delete;

	impl_type (x509_ptr &&x509, const std::span<const std::byte> &der)
		: x509{std::move(x509)}
		, bytes_buf{der.size()}
		, bytes{init_bytes(der)}
		, serial_number{init_serial_number()}
		, common_name{init_common_name()}
		, fingerprint{init_fingerprint()}
		, not_before{to_time(::X509_get_notBefore(this->x509.get()))}
		, not_after{to_time(::X509_get_notAfter(this->x509.get()))}
	{ }

	impl_type (x509_ptr &&x509)
		: x509{std::move(x509)}
		, bytes_buf{static_cast<size_t>(::i2d_X509(this->x509.get(), nullptr))}
		, bytes{init_bytes()}
		, serial_number{init_serial_number()}
		, common_name{init_common_name()}
		, fingerprint{init_fingerprint()}
		, not_before{to_time(::X509_get_notBefore(this->x509.get()))}
		, not_after{to_time(::X509_get_notAfter(this->x509.get()))}
	{ }

	std::span<const std::byte> init_bytes (const std::span<const std::byte> &der) noexcept
	{
		auto p = static_cast<std::byte *>(bytes_buf.get());
		std::uninitialized_copy(der.begin(), der.end(), p);
		return {p, der.size()};
	}

	std::span<const std::byte> init_bytes () noexcept
	{
		auto first = static_cast<std::byte *>(bytes_buf.get());
		auto last = first;
		::i2d_X509(x509.get(), reinterpret_cast<uint8_t **>(&last));
		return {first, last};
	}

	std::span<const uint8_t> init_serial_number () noexcept
	{
		auto sn = ::X509_get0_serialNumber(x509.get());
		return {sn->data, static_cast<size_t>(sn->length)};
	}

	std::string_view init_common_name () noexcept
	{
		const char *name = "";
		auto subject = ::X509_get_subject_name(x509.get());
		for (auto i = 0;  i != ::X509_NAME_entry_count(subject);  ++i)
		{
			auto subject_entry = ::X509_NAME_get_entry(subject, i);
			if (::OBJ_obj2nid(::X509_NAME_ENTRY_get_object(subject_entry)) == NID_commonName)
			{
				name = reinterpret_cast<const char *>(
					::ASN1_STRING_get0_data(::X509_NAME_ENTRY_get_data(subject_entry))
				);
				break;
			}
		}
		return name;
	}

	std::string_view init_fingerprint () noexcept
	{
		sha1_hash::digest_type fingerprint;
		::X509_digest(x509.get(), ::EVP_sha1(), fingerprint.data(), nullptr);
		encode<hex>(fingerprint, fingerprint_buf);
		fingerprint_buf[sizeof(fingerprint_buf) - 1] = '\0';
		return fingerprint_buf;
	}

	static result<impl_ptr> make_forward_list (x509_ptr &&first, x509_stack_ptr &&chain) noexcept
	{
		return pal::make_shared<impl_type>(std::move(first)).and_then([&](impl_ptr &&head) -> result<impl_ptr>
		{
			for (auto tail = head; ::sk_X509_num(chain.get()) > 0; tail = tail->next)
			{
				first.reset(sk_X509_pop(chain.get()));
				auto node = pal::make_shared<impl_type>(std::move(first));
				if (!node)
				{
					return unexpected{node.error()};
				}
				tail->next = std::move(*node);
			}
			return head;
		});
	}

	static constexpr auto to_api = [](impl_ptr &&value) -> certificate
	{
		return {std::move(value)};
	};
};

result<certificate> certificate::import_der (const std::span<const std::byte> &der) noexcept
{
	auto data = reinterpret_cast<const uint8_t *>(der.data());
	if (auto x509 = to_ptr(::d2i_X509(nullptr, &data, static_cast<long>(der.size()))))
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
	return ::X509_get_version(impl_->x509.get()) + 1;
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

alternative_name_value certificate::subject_alternative_name_value () const noexcept
{
	auto names = to_ptr(static_cast<::GENERAL_NAMES *>(::X509_get_ext_d2i(impl_->x509.get(), NID_subject_alt_name, nullptr, nullptr)));

	alternative_name_value result{};
	auto *p = result.data_, * const end = p + sizeof(result.data_);
	size_t index_size = 0;

	auto count = ::sk_GENERAL_NAME_num(names.get());
	for (auto i = 0; i < count && index_size < result.max_index_size && p < end; ++i)
	{
		size_t value_size = 0;

		const auto &entry = *sk_GENERAL_NAME_value(names.get(), i);
		if (entry.type == GEN_DNS)
		{
			value_size = copy_string(entry.d.dNSName, p, end);
		}
		else if (entry.type == GEN_EMAIL)
		{
			value_size = copy_string(entry.d.rfc822Name, p, end);
		}
		else if (entry.type == GEN_IPADD)
		{
			value_size = copy_string(entry.d.iPAddress, p, end);
		}
		else if (entry.type == GEN_URI)
		{
			value_size = copy_string(entry.d.uniformResourceIdentifier, p, end);
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

	impl_type (certificate::impl_ptr &&head) noexcept
		: head{std::move(head)}
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

result<certificate_store> certificate_store::import_pkcs12 (const std::span<const std::byte> &pkcs12, const char *password) noexcept
{
	auto data = reinterpret_cast<const uint8_t *>(pkcs12.data());
	std::unique_ptr<::PKCS12, decltype(&::PKCS12_free)> p12
	{
		::d2i_PKCS12(nullptr, &data, pkcs12.size_bytes()),
		&::PKCS12_free
	};

	X509 *first = nullptr;
	EVP_PKEY *first_private_key = nullptr;
	STACK_OF(X509) *chain = nullptr;
	if (!p12 || !::PKCS12_parse(p12.get(), password, &first_private_key, &first, &chain))
	{
		return make_unexpected(std::errc::invalid_argument);
	}

	auto pkey = to_ptr(first_private_key);
	return certificate::impl_type::make_forward_list(to_ptr(first), to_ptr(chain))
		.and_then(pal::make_shared<impl_type, certificate::impl_ptr>)
		.and_then([&pkey](auto &&store){ store->head->private_key = std::move(pkey); })
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
	::X509_NAME &name;
	const size_t size;

	impl_type (const certificate::impl_ptr &owner, ::X509_NAME *name, size_t size) noexcept
		: owner{owner}
		, name{*name}
		, size{size}
	{ }

	static constexpr auto to_api = [](impl_ptr &&value) -> distinguished_name
	{
		return {std::move(value)};
	};

	static result<distinguished_name> make (const certificate::impl_ptr &owner, ::X509_NAME *name) noexcept
	{
		auto size = static_cast<size_t>(::X509_NAME_entry_count(name));
		return pal::make_shared<impl_type>(owner, name, size).transform(to_api);
	}
};

void distinguished_name::const_iterator::load_next_entry () noexcept
{
	if (at_ < owner_->size)
	{
		auto entry = ::X509_NAME_get_entry(&owner_->name, at_++);

		copy_oid(::X509_NAME_ENTRY_get_object(entry), oid_);
		entry_.oid = oid_;

		copy_string(::X509_NAME_ENTRY_get_data(entry), value_, value_ + sizeof(value_));
		entry_.value = value_;

		return;
	}

	owner_ = nullptr;
}

result<distinguished_name> certificate::issuer_name () const noexcept
{
	return distinguished_name::impl_type::make(impl_, ::X509_get_issuer_name(impl_->x509.get()));
}

result<distinguished_name> certificate::subject_name () const noexcept
{
	return distinguished_name::impl_type::make(impl_, ::X509_get_subject_name(impl_->x509.get()));
}

// alternative_name {{{1

struct alternative_name::impl_type
{
	certificate::impl_ptr owner;
	name_list_ptr name;
	const size_t size;

	impl_type (certificate::impl_ptr owner, name_list_ptr &&name) noexcept
		: owner{owner}
		, name{std::move(name)}
		, size{static_cast<size_t>(::sk_GENERAL_NAME_num(this->name.get()))}
	{ }

	static constexpr auto to_api = [](impl_ptr &&value) -> alternative_name
	{
		return {std::move(value)};
	};

	static result<alternative_name> make (certificate::impl_ptr owner, int id) noexcept
	{
		if (auto name = to_ptr(static_cast<::GENERAL_NAMES *>(::X509_get_ext_d2i(owner->x509.get(), id, nullptr, nullptr))))
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
		const auto &entry = *sk_GENERAL_NAME_value(owner_->name.get(), at_++);
		switch (entry.type)
		{
			case GEN_DNS:
				copy_string(entry.d.dNSName, entry_value_, entry_value_ + sizeof(entry_value_));
				entry_.emplace<dns_name>(entry_value_);
				return;

			case GEN_EMAIL:
				copy_string(entry.d.rfc822Name, entry_value_, entry_value_ + sizeof(entry_value_));
				entry_.emplace<email_address>(entry_value_);
				return;

			case GEN_IPADD:
				copy_ip(entry.d.iPAddress, entry_value_, entry_value_ + sizeof(entry_value_));
				entry_.emplace<ip_address>(entry_value_);
				return;

			case GEN_URI:
				copy_string(entry.d.uniformResourceIdentifier, entry_value_, entry_value_ + sizeof(entry_value_));
				entry_.emplace<uri>(entry_value_);
				return;
		}
	}

	owner_ = nullptr;
	entry_.emplace<std::monostate>();
}

result<alternative_name> certificate::issuer_alternative_name () const noexcept
{
	return alternative_name::impl_type::make(impl_, NID_issuer_alt_name);
}

result<alternative_name> certificate::subject_alternative_name () const noexcept
{
	return alternative_name::impl_type::make(impl_, NID_subject_alt_name);
}

// key {{{1

struct key::impl_type
{
	certificate::impl_ptr owner;
	const ::EVP_PKEY &pkey;
	const key_algorithm algorithm;
	const size_t size_bits, max_block_size;

	impl_type (certificate::impl_ptr owner, const ::EVP_PKEY &pkey) noexcept
		: owner{owner}
		, pkey{pkey}
		, algorithm{to_key_algorithm(::EVP_PKEY_get_base_id(&pkey))}
		, size_bits{static_cast<size_t>(::EVP_PKEY_bits(&pkey))}
		, max_block_size{static_cast<size_t>(::EVP_PKEY_size(&pkey))}
	{ }

	static constexpr key_algorithm to_key_algorithm (int id) noexcept
	{
		switch (id)
		{
			case EVP_PKEY_RSA:
				return key_algorithm::rsa;
		}
		return key_algorithm::opaque;
	}

	static constexpr auto to_api = [](impl_ptr &&value) -> key
	{
		return {std::move(value)};
	};
};

key_algorithm key::algorithm () const noexcept
{
	return impl_->algorithm;
}

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
	const auto &pkey = *::X509_get0_pubkey(impl_->x509.get());
	return pal::make_shared<key::impl_type>(impl_, pkey).transform(key::impl_type::to_api);
}

result<key> certificate::private_key () const noexcept
{
	if (impl_->private_key)
	{
		return pal::make_shared<key::impl_type>(impl_, *(impl_->private_key.get())).transform(key::impl_type::to_api);
	}
	return make_unexpected(std::errc::io_error);
}

//}}}1

} // namespace pal::crypto
