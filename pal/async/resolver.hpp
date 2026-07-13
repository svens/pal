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
#include <utility>

namespace pal::async
{

namespace __resolver
{

/// start_resolve's op scratch state. The leading \ref __thread_pool::record is written by the pool at post
/// time and preserved by the work closure: the post-back wrapper reads the origin loop from it (see the
/// record contract in pal/async/thread_pool.hpp).
template <typename Protocol>
struct op_state
{
	__thread_pool::record record;
	[[no_unique_address]] net::ip::basic_resolver<Protocol> resolver;
	std::string_view name;
	std::string_view service;
	net::ip::resolver_base::flags flags;
	std::error_code ec;
	size_t count;
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

	handle (handle &&) noexcept = default;
	handle &operator= (handle &&) noexcept = default;
	~handle () noexcept = default;

	/// \copydoc start_resolve(task_ptr &&, std::string_view, std::string_view, flags, H)
	template <typename H>
	void start_resolve (task_ptr &&t, std::string_view name, std::string_view service, H handler) noexcept
		requires __async::handler<H, void(task_ptr &&, result<std::span<const endpoint_type>> &&) noexcept>
	{
		start_resolve(std::move(t), name, service, flags{}, std::move(handler));
	}

	/// Translate \a name and \a service to endpoints on a worker thread, then run \a handler on the
	/// owning loop's thread (from a subsequent run()). Any address family (AF_UNSPEC), shaped only via
	/// \a f; socket type and protocol come from \a Protocol.
	///
	/// Endpoints are written into the task's payload storage (\ref task::span, which itself stays
	/// untouched); the typed span passed to \a handler views them and caps at the payload capacity --
	/// excess results are silently dropped, as with the synchronous resolver's max_size(). The span (and
	/// the endpoints) stay valid until the storage is reused.
	///
	/// Contracts: loop-thread-only, like \ref event_loop::post_after. \a name and \a service must
	/// outlive the operation -- the worker reads them when it runs. Task payload storage must be attached,
	/// non-empty and aligned for \a endpoint_type. \ref net::ip::resolver_base::canonical_name is not
	/// supported (the completion carries no channel for a name); requesting it is a contract violation.
	template <typename H>
	void start_resolve (task_ptr &&t, std::string_view name, std::string_view service, flags f, H handler) noexcept
		requires __async::handler<H, void(task_ptr &&, result<std::span<const endpoint_type>> &&) noexcept>
	{
		static_assert(std::is_trivially_copyable_v<endpoint_type>);

		const auto payload = t->span();
		pal_require(!payload.empty(), "start_resolve without task payload storage");
		pal_require(
			reinterpret_cast<uintptr_t>(payload.data()) % alignof(endpoint_type) == 0,
			"start_resolve payload misaligned for endpoint storage"
		);
		pal_require(
			(f & net::ip::resolver_base::canonical_name) == 0,
			"start_resolve with canonical_name: completion carries no channel for it"
		);

		t->scratch_as<op_state>() = {
			.record = {.origin = loop_},
			.resolver = resolver_,
			.name = name,
			.service = service,
			.flags = f,
			.ec = {},
			.count = 0,
		};

		auto work = [] (task &w) noexcept
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

		auto wrapper = [h = std::move(handler)] (task_ptr &&p) mutable noexcept
		{
			const auto &op = p->scratch_as<op_state>();
			--op.record.origin->stats_.offload_in_flight;
			const auto ec = op.ec;
			const auto count = op.count;
			if (ec)
			{
				h(std::move(p), unexpected{ec});
			}
			else
			{
				const auto *first = reinterpret_cast<const endpoint_type *>(p->span().data());
				h(std::move(p), std::span<const endpoint_type>{first, count});
			}
		};

		using closure_type = __thread_pool::closure<decltype(work), decltype(wrapper)>;
		static_assert(
			sizeof(closure_type) <= __async::closure_capacity,
			"resolve wrapper and handler closures exceed the closure budget"
		);

		++loop_->stats_.offload_in_flight;
		t->bind<__thread_pool::op_execute>(closure_type{std::move(work), std::move(wrapper)});
		__thread_pool::submit(*pool_, *t.release());
	}

private:

	using op_state = __resolver::op_state<Protocol>;
	static_assert(std::is_standard_layout_v<op_state>); // record at offset 0, per its contract

	[[no_unique_address]] net::ip::basic_resolver<Protocol> resolver_;
	__thread_pool::impl_type *pool_;
	__event_loop::impl_type *loop_;

	handle (net::ip::basic_resolver<Protocol> &&resolver,
		__thread_pool::impl_type &pool,
		__event_loop::impl_type &loop) noexcept
		: resolver_{std::move(resolver)}
		, pool_{&pool}
		, loop_{&loop}
	{
	}

	friend class event_loop;
};

} // namespace pal::async
