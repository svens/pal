#pragma once

/**
 * \file pal/net/ip/basic_resolver.hpp
 * Host/service name resolution
 */

#include <pal/net/__socket.hpp>
#include <pal/result.hpp>
#include <array>
#include <iterator>
#include <memory>
#include <string_view>

#if __pal_net_posix
	#include <netdb.h>
#elif __pal_net_winsock
	#include <ws2tcpip.h>
#endif

namespace pal::net::ip
{

template <typename InternetProtocol>
class basic_resolver_entry;

template <typename InternetProtocol>
class basic_resolver_results_iterator;

template <typename InternetProtocol>
class basic_resolver_results;

template <typename InternetProtocol>
class basic_resolver;

/// Resolver error codes
enum class resolver_errc : int
{
	host_not_found = EAI_NONAME,
	service_not_found = EAI_SERVICE,
	try_again = EAI_AGAIN,
};

/// Name resolution error category
const std::error_category &resolver_category () noexcept;

/// Return error code with \a ec and resolver_category()
inline std::error_code make_error_code (resolver_errc ec) noexcept
{
	return {static_cast<int>(ec), resolver_category()};
}

namespace __resolver
{

// NOLINTBEGIN(misc-non-private-member-variables-in-classes)

/// Common result fields shared by forward and reverse lookups.
/// endpoint_data points to the start of a flat array of endpoint-sized slots;
/// max_count is the upper bound on the number of slots (forward result only).
struct result_base
{
	virtual ~result_base () noexcept = default;

	static constexpr size_t max_count = 16;

	std::array<char, NI_MAXHOST> host_buf{};
	std::array<char, NI_MAXSERV> service_buf{};
	std::string_view host;
	std::string_view service;
	size_t count{};
	const char *endpoint_data{};
};

// NOLINTEND(misc-non-private-member-variables-in-classes)

using result_storage_ptr = std::unique_ptr<result_base>;

} // namespace __resolver

/// Single element in the result set returned by a name resolution operation
template <typename InternetProtocol>
class basic_resolver_entry
{
public:

	using protocol_type = InternetProtocol;
	using endpoint_type = InternetProtocol::endpoint;

	/// Return endpoint associated with this entry
	[[nodiscard]] const endpoint_type &endpoint () const noexcept
	{
		return *reinterpret_cast<const endpoint_type *>(current_);
	}

	/// Return endpoint associated with this entry
	operator endpoint_type () const noexcept
	{
		return endpoint();
	}

	/// Return host name associated with this entry
	[[nodiscard]] std::string_view host_name () const noexcept
	{
		return result_->host;
	}

	/// Return service name associated with this entry
	[[nodiscard]] std::string_view service_name () const noexcept
	{
		return result_->service;
	}

private:

	const __resolver::result_base *result_{};
	const char *current_{};

	basic_resolver_entry () noexcept = default;

	basic_resolver_entry (const __resolver::result_base *result, const char *current) noexcept
		: result_{result}
		, current_{current}
	{
	}

	friend class basic_resolver_results_iterator<InternetProtocol>;
};

/// Forward iterator over entries in a basic_resolver_results range
template <typename InternetProtocol>
class basic_resolver_results_iterator
{
public:

	using difference_type = std::ptrdiff_t;
	using value_type = basic_resolver_entry<InternetProtocol>;
	using reference = const value_type &;
	using pointer = const value_type *;
	using iterator_category = std::forward_iterator_tag;

	basic_resolver_results_iterator () noexcept = default;

	[[nodiscard]] reference operator* () const noexcept
	{
		return entry_;
	}

	[[nodiscard]] pointer operator->() const noexcept
	{
		return &entry_;
	}

	basic_resolver_results_iterator &operator++ () noexcept
	{
		entry_.current_ += sizeof(typename value_type::endpoint_type);
		return *this;
	}

	basic_resolver_results_iterator operator++ (int) noexcept
	{
		auto tmp = *this;
		++*this;
		return tmp;
	}

	[[nodiscard]] bool operator== (const basic_resolver_results_iterator &that) const noexcept
	{
		return entry_.current_ == that.entry_.current_;
	}

private:

	value_type entry_{};

	basic_resolver_results_iterator (const __resolver::result_base *result, const char *current) noexcept
		: entry_{result, current}
	{
	}

	friend class basic_resolver_results<InternetProtocol>;
};

/// Sequence of basic_resolver_entry elements from a single name resolution operation
template <typename InternetProtocol>
class basic_resolver_results
{
public:

	using protocol_type = InternetProtocol;
	using endpoint_type = InternetProtocol::endpoint;
	using value_type = basic_resolver_entry<InternetProtocol>;
	using const_reference = const value_type &;
	using reference = value_type &;
	using const_iterator = basic_resolver_results_iterator<InternetProtocol>;
	using iterator = const_iterator;
	using difference_type = std::ptrdiff_t;
	using size_type = size_t;

	/// Return number of entries
	[[nodiscard]] size_type size () const noexcept
	{
		return storage_->count;
	}

	/// Return maximum possible result set size
	[[nodiscard]] static constexpr size_type max_size () noexcept
	{
		return __resolver::result_base::max_count;
	}

	/// Return true if result set contains no entries
	[[nodiscard]] bool empty () const noexcept
	{
		return size() == 0;
	}

	/// Return iterator to the first entry
	[[nodiscard]] const_iterator begin () const noexcept
	{
		return {storage_.get(), storage_->endpoint_data};
	}

	/// \copydoc begin()
	[[nodiscard]] const_iterator cbegin () const noexcept
	{
		return begin();
	}

	/// Return past-the-end iterator
	[[nodiscard]] const_iterator end () const noexcept
	{
		return {storage_.get(), storage_->endpoint_data + (storage_->count * sizeof(endpoint_type))};
	}

	/// \copydoc end()
	[[nodiscard]] const_iterator cend () const noexcept
	{
		return end();
	}

private:

	__resolver::result_storage_ptr storage_;

	explicit basic_resolver_results (__resolver::result_storage_ptr &&storage) noexcept
		: storage_{std::move(storage)}
	{
	}

	friend class basic_resolver<InternetProtocol>;
};

/// Base type providing resolver flags and protected name resolution primitives
class resolver_base
{
public:

	/// \defgroup resolver_flags Resolver flags
	/// \{

	using flags = int;

	/// Return endpoints for locally bound sockets (pass null host to getaddrinfo)
	static constexpr flags passive = AI_PASSIVE;

	/// Determine the canonical name of the host
	static constexpr flags canonical_name = AI_CANONNAME;

	/// Host name is a numeric IPv4 or IPv6 address string
	static constexpr flags numeric_host = AI_NUMERICHOST;

	/// Service name is a numeric port number string
	static constexpr flags numeric_service = AI_NUMERICSERV;

	/// Return IPv4-mapped IPv6 addresses when no IPv6 addresses are found
	static constexpr flags v4_mapped = AI_V4MAPPED;

	/// Combined with v4_mapped: return all matching IPv6 and IPv4 addresses
	static constexpr flags all_matching = AI_ALL;

	/// Return only addresses for which a non-loopback interface of the family is configured
	static constexpr flags address_configured = AI_ADDRCONFIG;

	/// \}

protected:

	~resolver_base () = default;

	[[nodiscard]] static result<__resolver::result_storage_ptr>
	resolve (std::string_view host, std::string_view service, const ::addrinfo &hints) noexcept;

	[[nodiscard]] static result<__resolver::result_storage_ptr>
	resolve (const void *name, size_t namelen, int type) noexcept;
};

/// Translates host/service names to endpoints and endpoints to host/service names
template <typename InternetProtocol>
class basic_resolver: public resolver_base
{
public:

	using protocol_type = InternetProtocol;
	using endpoint_type = InternetProtocol::endpoint;
	using results_type = basic_resolver_results<InternetProtocol>;

	/// Translate \a host and \a service to a set of endpoints using any address family
	[[nodiscard]] result<results_type>
	resolve (std::string_view host, std::string_view service, flags f = {}) noexcept
	{
		// endpoint_type{} uses the v4 sockaddr path; .protocol() calls protocol_type{int},
		// not the deleted protocol_type() default ctor (e.g., udp() = delete).
		static constexpr auto proto = endpoint_type{}.protocol();
		::addrinfo hints{};
		hints.ai_flags = f;
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = proto.type();
		hints.ai_protocol = proto.protocol();
		return resolver_base::resolve(host, service, hints).transform(to_results);
	}

	/// Translate \a host and \a service to a set of endpoints matching \a protocol
	[[nodiscard]] result<results_type>
	resolve (const protocol_type &protocol, std::string_view host, std::string_view service, flags f = {}) noexcept
	{
		::addrinfo hints{};
		hints.ai_flags = f;
		hints.ai_family = protocol.family();
		hints.ai_socktype = protocol.type();
		hints.ai_protocol = protocol.protocol();
		return resolver_base::resolve(host, service, hints).transform(to_results);
	}

	/// Translate \a endpoint to its corresponding host and service names
	[[nodiscard]] result<results_type> resolve (const endpoint_type &endpoint) noexcept
	{
		return resolver_base::resolve(endpoint.data(), endpoint.size(), endpoint.protocol().type())
			.transform(to_results);
	}

private:

	static constexpr auto to_results = [] (__resolver::result_storage_ptr &&ptr)
	{
		return results_type{std::move(ptr)};
	};
};

} // namespace pal::net::ip

namespace pal
{

/// Return unexpected result with \a ec and resolver_category()
inline unexpected make_unexpected (net::ip::resolver_errc ec) noexcept
{
	return unexpected{net::ip::make_error_code(ec)};
}

} // namespace pal

namespace std
{

template <>
struct is_error_code_enum<pal::net::ip::resolver_errc>: true_type
{
};

} // namespace std
