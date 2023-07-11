#include <pal/crypto/hash>
#include <pal/memory>
#include <openssl/asn1.h>
#include <openssl/x509.h>

namespace pal::crypto {

namespace {

certificate::time_type to_time (const ::ASN1_TIME *time) noexcept
{
	std::tm tm{};
	::ASN1_TIME_to_tm(time, &tm);
	return certificate::clock_type::from_time_t(std::mktime(&tm));
}

} // namespace

struct certificate::impl_type
{
	using x509_ptr = std::unique_ptr<::X509, decltype(&::X509_free)>;
	x509_ptr x509;

	temporary_buffer<2048> bytes_buf;
	std::span<const std::byte> bytes;

	std::span<const uint8_t> serial_number;

	std::string_view common_name;

	char fingerprint_buf[hex::encode_size(sha1_hash::digest_size) + 1];
	std::string_view fingerprint;

	time_type not_before, not_after;

	impl_type (x509_ptr &&x509, const std::span<const std::byte> &der) noexcept
		: x509{std::move(x509)}
		, bytes_buf{std::nothrow, der.size()}
		, bytes{init_bytes(der)}
		, serial_number{init_serial_number()}
		, common_name{init_common_name()}
		, fingerprint{init_fingerprint()}
		, not_before{to_time(::X509_get_notBefore(this->x509.get()))}
		, not_after{to_time(::X509_get_notAfter(this->x509.get()))}
	{ }

	std::span<const std::byte> init_bytes (const std::span<const std::byte> &der) noexcept
	{
		auto p = static_cast<std::byte *>(bytes_buf.get());
		if (p)
		{
			std::uninitialized_copy(der.begin(), der.end(), p);
		}
		return {p, der.size()};
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
};

result<certificate> certificate::import_der (std::span<const std::byte> der) noexcept
{
	auto data = reinterpret_cast<const uint8_t *>(der.data());
	impl_type::x509_ptr x509
	{
		::d2i_X509(nullptr, &data, static_cast<long>(der.size())),
		&::X509_free
	};

	if (x509)
	{
		impl_ptr impl{new(std::nothrow) impl_type(std::move(x509), der)};
		if (impl && impl->bytes.data())
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

struct distinguished_name::impl_type
{
	certificate::impl_ptr owner;
	::X509_NAME &name;
	size_t entries;

	impl_type (certificate::impl_ptr owner, ::X509_NAME *name, size_t entries) noexcept
		: owner{owner}
		, name{*name}
		, entries{entries}
	{ }

	static result<distinguished_name> make (certificate::impl_ptr owner, ::X509_NAME *name) noexcept
	{
		impl_ptr list = nullptr;

		if (auto entries = static_cast<size_t>(::X509_NAME_entry_count(name)))
		{
			list.reset(new(std::nothrow) impl_type(owner, name, entries));
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
void copy (const ::ASN1_STRING *s, char (&buf)[N]) noexcept
{
	std::strncpy(buf, reinterpret_cast<const char *>(::ASN1_STRING_get0_data(s)), N - 1);
	buf[N - 1] = '\0';
}

template <size_t N>
void copy (const ::ASN1_OBJECT *o, char (&buf)[N]) noexcept
{
	::OBJ_obj2txt(buf, N, o, 1);
}

} // namespace

void distinguished_name::const_iterator::load_entry_at (size_t index) noexcept
{
	if (index < owner_->entries)
	{
		auto entry = ::X509_NAME_get_entry(&owner_->name, index);
		copy(::X509_NAME_ENTRY_get_object(entry), entry_.oid);
		copy(::X509_NAME_ENTRY_get_data(entry), entry_.value);
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

} // namespace pal::crypto
