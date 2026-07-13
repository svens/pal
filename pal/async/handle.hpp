#pragma once

/**
 * \file pal/async/handle.hpp
 * Async resource handle primary template
 */

#include <pal/async/event_loop.hpp>
#include <pal/async/thread_pool.hpp>
#include <pal/result.hpp>
#include <utility>

namespace pal::async
{

/// Async handle to a synchronous resource consumed by \ref event_loop::make_handle: async operations plus
/// a curated read-mostly subset of the resource's own API. One-way -- there is no release back to sync;
/// destruction closes the resource (subject to the teardown contract). Defined per resource type as a
/// specialization in its own header (e.g. pal/async/resolver.hpp); there is no generic handle.
template <typename T>
class handle;

template <typename T>
result<handle<T>> event_loop::make_handle (T resource, thread_pool &pool) noexcept
{
	return handle<T>{std::move(resource), *pool.impl_, *impl_};
}

} // namespace pal::async
