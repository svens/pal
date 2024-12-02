#include <pal/net/ip/basic_resolver>

namespace pal::net::ip {

const std::error_category &resolver_category () noexcept
{
	struct impl_type: public std::error_category
	{
		virtual ~impl_type () = default;

		const char *name () const noexcept override final
		{
			return "resolver";
		}

		std::string message (int e) const override final
		{
			return ::gai_strerror(e);
		}
	};
	static const impl_type impl{};
	return impl;
}

int __resolver::address_info_list::get_address_info (
	std::string_view h,
	std::string_view s,
	int flags,
	int family,
	int type,
	int protocol) noexcept
{
	if (h.data())
	{
		h.copy(host_buf, sizeof(host_buf));
		host = host_buf;
	}

	if (s.data())
	{
		s.copy(service_buf, sizeof(service_buf));
		service = service_buf;
	}

	::addrinfo *result{}, hints{};
	hints.ai_flags = flags;
	hints.ai_family = family;
	hints.ai_socktype = type;
	hints.ai_protocol = protocol;

	auto r = ::getaddrinfo(host.data(), service.data(), &hints, &result);
	if (r == 0)
	{
		head.reset(result);
		for (/**/; result; result = result->ai_next, size++)
		{
			if (size == 0 && result->ai_canonname)
			{
				host = result->ai_canonname;
			}
		}
		return 0;
	}

	if (r == EAI_NONAME && host.empty())
	{
		r = EAI_SERVICE;
	}

	return r;
}

int __resolver::address_info_list::get_name_info (
	const void *name,
	size_t namelen,
	int type) noexcept
{
	int flags = type == SOCK_DGRAM ? NI_DGRAM : 0;

	auto r = ::getnameinfo(
		static_cast<const sockaddr *>(name),
		static_cast<socklen_t>(namelen),
		host_buf,
		sizeof(host_buf),
		service_buf,
		sizeof(service_buf),
		flags
	);

	if (r != 0)
	{
		flags |= NI_NUMERICSERV;
		r = ::getnameinfo(
			static_cast<const sockaddr *>(name),
			static_cast<socklen_t>(namelen),
			host_buf,
			sizeof(host_buf),
			service_buf,
			sizeof(service_buf),
			flags
		);
	}

	if (r != 0)
	{
		return r;
	}

	host = host_buf;
	service = service_buf;
	std::copy_n(static_cast<const char *>(name), namelen, data);
	info.ai_addr = reinterpret_cast<sockaddr *>(data);
	info.ai_addrlen = namelen;
	size = 1;

	head = {&info, [](::addrinfo *){}};
	return 0;
}

namespace {

using namespace __resolver;

} // namespace

result<address_info_list_ptr> resolver_base::resolve (
	std::string_view host,
	std::string_view service,
	resolver_base::flags flags,
	int family,
	int type,
	int protocol) noexcept
{
	if (auto result = address_info_list_ptr{new(std::nothrow) address_info_list})
	{
		auto r = result->get_address_info(host, service, flags, family, type, protocol);
		if (r == 0)
		{
			return result;
		}
		else if (r != EAI_MEMORY)
		{
			return make_unexpected(resolver_errc{r});
		}
	}
	return make_unexpected(std::errc::not_enough_memory);
}

result<address_info_list_ptr> resolver_base::resolve (
	const void *name,
	size_t namelen,
	int type) noexcept
{
	if (auto result = address_info_list_ptr{new(std::nothrow) address_info_list})
	{
		auto r = result->get_name_info(name, namelen, type);
		if (r == 0)
		{
			return result;
		}
		else if (r != EAI_MEMORY)
		{
			return make_unexpected(resolver_errc{r});
		}
	}
	return make_unexpected(std::errc::not_enough_memory);
}

} // namespace pal::net::ip
