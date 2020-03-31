#include <pal/error>
#include <string>


__pal_begin


namespace {

constexpr std::string_view to_string_view (errc ec) noexcept
{
	switch (ec)
	{
		#define __pal_errc_case(code, message) case pal::errc::code: return message;
		__pal_errc(__pal_errc_case)
		#undef __pal_errc_case
	};
	return "unknown";
}

} // namespace


const std::error_category &error_category () noexcept
{
	struct error_category_impl
		: public std::error_category
	{
		[[nodiscard]] const char *name () const noexcept final
		{
			return "pal";
		}

		[[nodiscard]] std::string message (int ec) const final
		{
			return std::string{to_string_view(static_cast<errc>(ec))};
		}
	};
	static const error_category_impl impl{};
	return impl;
}


__pal_end
