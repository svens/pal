#pragma once

/**
 * \file pal/async/multishot.hpp
 * Multishot resource wrapper consumed by event_loop::make_handle
 */

#include <pal/async/__async.hpp>
#include <pal/async/event_loop.hpp>
#include <cstddef>
#include <span>

namespace pal::async
{

/// Sizing of the task pool a \ref multishot handle owns.
struct multishot_config
{
	/// Tasks in the pool: inbound completions the application can hold at once.
	/// Must be in [1, \ref max_task_count].
	static constexpr size_t max_task_count = 32768;
	size_t task_count = 0;

	/// Payload capacity of each task. Larger datagrams are delivered truncated.
	/// Must be in [1, \ref max_buffer_size].
	static constexpr size_t max_buffer_size = 65536;
	size_t buffer_size = 0;
};

/// Selects multishot operation for \a Resource at \ref event_loop::make_handle.
///
/// The resource's inbound operation runs from make_handle until the handle is destroyed, completing into
/// tasks from a pool the handle owns, sized by \ref config. Each completion lends its task to \ref handler:
/// dropping the task returns it to the pool, starting an operation with it hands it back to the library.
/// Outbound operations stay single-shot. The handle type is <tt>handle<multishot<Resource>></tt>,
/// independent of \a Handler.
template <typename Resource, typename Handler>
struct multishot
{
	/// Resource consumed by make_handle
	Resource resource;

	/// Pool sizing
	multishot_config config;

	/// Handler invoked for every inbound completion
	Handler handler;
};

/// \cond
template <typename Resource>
struct multishot<Resource, void>;

template <typename Resource, typename Handler>
multishot (Resource, multishot_config, Handler) -> multishot<Resource, Handler>;
/// \endcond

namespace __event_loop
{

struct multishot_pool: __task::recycler
{
	socket_state *owner;
	const size_t task_count;
	const size_t buffer_size;
	size_t borrowed = 0;
	__async::completion<task> handler{};

	void detach () noexcept;

	[[nodiscard]] std::span<std::byte> buffer (task &t) const noexcept
	{
		return {reinterpret_cast<std::byte *>(&t) + sizeof(task), buffer_size};
	}
};

result<multishot_pool_ptr> make_multishot_pool (socket_state &owner, const multishot_config &config) noexcept;

} // namespace __event_loop

} // namespace pal::async
