#pragma once

/**
 * \file pal/async/resolver.hpp
 * Asynchronous forward name resolution
 */

#include <pal/async/handle.hpp>
#include <pal/net/ip/basic_resolver.hpp>
#include <pal/require.hpp>
#include <pal/result.hpp>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

namespace pal::async
{

namespace __resolver
{

template <typename Protocol>
struct op_state
{
	[[no_unique_address]] net::ip::basic_resolver<Protocol> resolver;
	std::string_view name;
	std::string_view service;
	net::ip::resolver_base::flags flags;
	std::error_code ec;
	size_t count;
	bool truncated;
};

} // namespace __resolver

/// Asynchronous forward name resolution for \a Protocol: \ref start_resolve runs the blocking lookup on
/// the thread_pool given to \ref event_loop::make_handle and completes on the owning loop's thread.
/// Reverse lookup stays on the synchronous \c basic_resolver.
template <typename Protocol>
class handle<net::ip::basic_resolver<Protocol>>
{
public:

	using protocol_type = Protocol;
	using endpoint_type = Protocol::endpoint;

	/// \copydoc net::ip::resolver_base::flags
	using flags = net::ip::resolver_base::flags;

	/// Completion payload of \ref start_resolve
	struct resolve_result
	{
		/// Resolved endpoints, viewing the task's payload storage: valid until that storage is reused
		std::span<const endpoint_type> endpoints;

		/// True if more endpoints resolved than the payload storage holds; \ref endpoints has the first ones
		bool truncated;
	};

	handle (handle &&) noexcept = default;
	handle &operator= (handle &&) noexcept = default;
	~handle () noexcept = default;

	/// \copydoc start_resolve(task_ptr &&, std::string_view, std::string_view, flags, H)
	template <typename H>
	void start_resolve (task_ptr &&t, std::string_view name, std::string_view service, H handler) noexcept
		requires __async::handler<H, void(task_ptr &&, result<resolve_result> &&) noexcept>
	{
		start_resolve(std::move(t), name, service, flags{}, std::move(handler));
	}

	/// Translate \a name and \a service to endpoints on a worker thread, then run \a handler on the
	/// owning loop's thread (from a subsequent run()). Any address family (AF_UNSPEC), shaped only via
	/// \a f; socket type and protocol come from \a Protocol.
	///
	/// Endpoints are written into the task's payload storage (\ref task::span, which itself stays
	/// untouched); \ref resolve_result::endpoints views them and caps at the payload capacity, with
	/// \ref resolve_result::truncated set when results were dropped.
	///
	/// Contracts: loop-thread-only, like \ref event_loop::post_after. \a name and \a service must
	/// outlive the operation -- the worker reads them when it runs. Task payload storage must be aligned for
	/// \a endpoint_type. \ref net::ip::resolver_base::canonical_name has no effect: the completion carries no
	/// channel for a name.
	template <typename H>
	void start_resolve (task_ptr &&t, std::string_view name, std::string_view service, flags f, H handler) noexcept
		requires __async::handler<H, void(task_ptr &&, result<resolve_result> &&) noexcept>
	{
		static_assert(std::is_trivially_copyable_v<endpoint_type>);

		pal_require(
			reinterpret_cast<uintptr_t>(t->span().data()) % alignof(endpoint_type) == 0,
			"start_resolve payload misaligned for endpoint storage"
		);

		t->scratch_as<op_state>() = {
			.resolver = resolver_,
			.name = name,
			.service = service,
			.flags = f,
			.ec = {},
			.count = 0,
			.truncated = false,
		};

		__thread_pool::submit(*pool_, *loop_, std::move(t), do_resolve(), on_ready(std::move(handler)));
	}

private:

	using op_state = __resolver::op_state<Protocol>;

	[[no_unique_address]] net::ip::basic_resolver<Protocol> resolver_;
	__event_loop::impl_type *loop_;
	__thread_pool::impl_type *pool_;

	handle (net::ip::basic_resolver<Protocol> &&resolver,
		__event_loop::impl_type &loop,
		__thread_pool::impl_type &pool) noexcept
		: resolver_{std::move(resolver)}
		, loop_{&loop}
		, pool_{&pool}
	{
	}

	static result<handle> make (net::ip::basic_resolver<Protocol> &&resolver,
		__event_loop::impl_type &loop,
		__thread_pool::impl_type &pool) noexcept
	{
		return handle{std::move(resolver), loop, pool};
	}

	static auto do_resolve () noexcept
	{
		return [](task &w) noexcept
		{
			auto &op = w.scratch_as<op_state>();
			if (auto r = op.resolver.resolve(op.name, op.service, op.flags); r)
			{
				const auto out = w.span();
				const size_t capacity = out.size() / sizeof(endpoint_type);
				for (const auto &entry: *r)
				{
					if (op.count == capacity)
					{
						op.truncated = true;
						break;
					}
					std::memcpy(
						out.data() + op.count * sizeof(endpoint_type),
						&entry.endpoint(),
						sizeof(endpoint_type)
					);
					++op.count;
				}
			}
			else
			{
				op.ec = r.error();
			}
		};
	}

	template <typename H>
	static auto on_ready (H handler) noexcept
	{
		return [h = std::move(handler)](task_ptr &&p) mutable noexcept
		{
			const auto &op = p->scratch_as<op_state>();
			if (const auto ec = op.ec; ec)
			{
				h(std::move(p), unexpected{ec});
			}
			else
			{
				const auto *first = reinterpret_cast<const endpoint_type *>(p->span().data());
				h(std::move(p), resolve_result{.endpoints = {first, op.count}, .truncated = op.truncated});
			}
		};
	}

	friend class event_loop;
};

} // namespace pal::async
