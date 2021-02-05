#include <pal/crypto/error>

#if __pal_os_macos //{{{1
	#include <pal/__bits/ref>
	#include <Security/SecBase.h>
	#include <CoreFoundation/CFString.h>
#elif __pal_os_linux //{{{1
	#include <openssl/err.h>
#endif //}}}1


__pal_begin


namespace crypto {


namespace {

constexpr const char *category_name = "crypto";

constexpr std::string_view as_view (int code) noexcept
{
	switch (static_cast<errc>(code))
	{
		#define __pal_crypto_errc_case(Code, Message) case pal::crypto::errc::Code: return Message;
		__pal_crypto_errc(__pal_crypto_errc_case)
		#undef __pal_crypto_errc_case
	};
	return "unknown";
}

} // namespace


const std::error_category &category () noexcept
{
	struct category_impl: public std::error_category
	{
		virtual ~category_impl ()
		{ }

		const char *name () const noexcept final override
		{
			return category_name;
		}

		std::string message (int code) const final override
		{
			return std::string{as_view(code)};
		}
	};
	static const category_impl cat{};
	return cat;
}


const std::error_category &system_category () noexcept
{
#if __pal_os_linux //{{{1

	struct category_impl: public std::error_category
	{
		category_impl () noexcept
		{
			#if OPENSSL_API_COMPAT < 0x10100000L
				ERR_load_crypto_strings();
			#endif
		}

		virtual ~category_impl ()
		{ }

		const char *name () const noexcept final override
		{
			return category_name;
		}

		std::string message (int code) const final override
		{
			char buf[256];
			return ERR_error_string(code, buf);
		}
	};
	static const category_impl cat{};
	return cat;

#elif __pal_os_macos //{{{1

	struct category_impl: public std::error_category
	{
		virtual ~category_impl ()
		{ }

		const char *name () const noexcept final override
		{
			return category_name;
		}

		std::string message (int code) const final override
		{
			char buf[256];
			if (unique_ref<::CFStringRef> s = ::SecCopyErrorMessageString(code, nullptr))
			{
				static constexpr auto encoding = ::kCFStringEncodingUTF8;
				if (auto p = ::CFStringGetCStringPtr(s.ref, encoding))
				{
					return p;
				}
				::CFStringGetCString(s.ref, buf, sizeof(buf), encoding);
			}
			else
			{
				snprintf(buf, sizeof(buf), "%s:%d", name(), code);
			}
			return buf;
		}
	};
	static const category_impl cat{};
	return cat;

#elif __pal_os_windows //{{{1

	return std::system_category();

#endif //}}}1
}


} // namespace crypto


__pal_end
