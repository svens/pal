#include <pal/net/ip/basic_resolver.hpp>
#include <algorithm>

namespace pal::net::ip
{

namespace
{

struct addrinfo_deleter
{
	void operator() (::addrinfo *p) const noexcept
	{
		::freeaddrinfo(p);
	}
};
using addrinfo_ptr = std::unique_ptr<::addrinfo, addrinfo_deleter>;

// NOLINTBEGIN(misc-non-private-member-variables-in-classes)

struct forward_result: __resolver::result_base
{
	static constexpr size_t slot_size = sizeof(::sockaddr_in6);
	alignas(::sockaddr_in6) std::array<char, max_count * slot_size> slots{};

	int fill (std::string_view h, std::string_view s, const ::addrinfo &hints) noexcept;
};

struct reverse_result: __resolver::result_base
{
	struct addr_info
	{
		const void *addr;
		socklen_t len;
		int sock_type;
	};

	union
	{
		std::array<char, sizeof(::sockaddr_in6)> data;
		::sockaddr_in v4;
		::sockaddr_in6 v6;
	};

	int fill (const addr_info &ai) noexcept;
};

// NOLINTEND(misc-non-private-member-variables-in-classes)

int forward_result::fill (std::string_view h, std::string_view s, const ::addrinfo &hints) noexcept
{
	const char *host_ptr = nullptr;
	const char *service_ptr = nullptr;

	if (h.data() != nullptr)
	{
		const auto len = (std::min)(h.size(), host_buf.size() - 1);
		std::copy_n(h.begin(), len, host_buf.begin());
		host = host_buf.data();
		host_ptr = host_buf.data();
	}

	if (s.data() != nullptr)
	{
		const auto len = (std::min)(s.size(), service_buf.size() - 1);
		std::copy_n(s.begin(), len, service_buf.begin());
		service = service_buf.data();
		service_ptr = service_buf.data();
	}

	addrinfo_ptr result;
	auto r = ::getaddrinfo(host_ptr, service_ptr, &hints, std::out_ptr(result));
	if (r != 0)
	{
		if (r == EAI_NONAME && host.empty())
		{
			r = EAI_SERVICE;
		}
		return r;
	}

	for (auto *p = result.get(); p != nullptr && count < max_count; p = p->ai_next, ++count)
	{
		if (count == 0 && p->ai_canonname != nullptr)
		{
			const std::string_view canon{p->ai_canonname};
			const auto len = (std::min)(canon.size(), host_buf.size() - 1);
			std::copy_n(canon.begin(), len, host_buf.begin());
			host = host_buf.data();
		}
		std::copy_n(
			reinterpret_cast<const char *>(p->ai_addr), p->ai_addrlen, slots.data() + (count * slot_size)
		);
	}

	endpoint_data = slots.data();
	return 0;
}

int reverse_result::fill (const addr_info &ai) noexcept
{
	const int flags = (ai.sock_type == SOCK_DGRAM) ? NI_DGRAM : 0;
	const auto *sa = static_cast<const sockaddr *>(ai.addr);

	auto r = ::getnameinfo(
		sa,
		ai.len,
		host_buf.data(),
		static_cast<socklen_t>(host_buf.size()),
		service_buf.data(),
		static_cast<socklen_t>(service_buf.size()),
		flags
	);

	if (r != 0)
	{
		r = ::getnameinfo(
			sa,
			ai.len,
			host_buf.data(),
			static_cast<socklen_t>(host_buf.size()),
			service_buf.data(),
			static_cast<socklen_t>(service_buf.size()),
			flags | NI_NUMERICSERV
		);
	}

	if (r != 0)
	{
		return r;
	}

	host = host_buf.data();
	service = service_buf.data();
	std::copy_n(static_cast<const char *>(ai.addr), ai.len, data.data());
	endpoint_data = data.data();
	count = 1;

	return 0;
}

template <typename Storage, typename... Args>
result<__resolver::result_storage_ptr> try_fill (Args &&...args) noexcept
{
	auto *raw = new (std::nothrow) Storage{};
	if (raw == nullptr)
	{
		return make_unexpected(std::errc::not_enough_memory);
	}
	__resolver::result_storage_ptr storage{raw};
	auto r = raw->fill(std::forward<Args>(args)...);
	if (r == 0)
	{
		return storage;
	}
	if (r == EAI_MEMORY)
	{
		return make_unexpected(std::errc::not_enough_memory);
	}
	return make_unexpected(resolver_errc{r});
}

} // namespace

const std::error_category &resolver_category () noexcept
{
	struct impl_type: std::error_category
	{
		[[nodiscard]] const char *name () const noexcept final
		{
			return "resolver";
		}

		[[nodiscard]] std::string message (int e) const final
		{
			return ::gai_strerror(e);
		}
	};
	static const impl_type impl{};
	return impl;
}

result<__resolver::result_storage_ptr>
resolver_base::resolve (std::string_view host, std::string_view service, const ::addrinfo &hints) noexcept
{
	return try_fill<forward_result>(host, service, hints);
}

result<__resolver::result_storage_ptr> resolver_base::resolve (const void *name, size_t namelen, int type) noexcept
{
	const reverse_result::addr_info ai{
		.addr = name,
		.len = static_cast<socklen_t>(namelen),
		.sock_type = type,
	};
	return try_fill<reverse_result>(ai);
}

} // namespace pal::net::ip
