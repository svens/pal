#include <pal/net/ip/basic_resolver>

__pal_begin

namespace net::ip {

const std::error_category &resolver_category () noexcept
{
	struct impl: public std::error_category
	{
		virtual ~impl () = default;

		const char *name () const noexcept final override
		{
			return "resolver";
		}

		std::string message (int e) const final override
		{
			return ::gai_strerror(e);
		}
	};
	static const impl cat{};
	return cat;
}

} // namespace net::ip

__pal_end
