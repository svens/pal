#include <pal/async/resolver.hpp>
#include <pal/net/ip/udp.hpp>
#include <pal/test.hpp>
#include <pal/version.hpp>
#include <catch2/catch_test_macros.hpp>
#include <array>
#include <atomic>
#include <chrono>
#include <span>
#include <thread>
#include <tuple>

namespace
{

using namespace pal::async;
using namespace std::chrono_literals;

using udp = pal::net::ip::udp;
using resolver = udp::resolver;
using endpoint = udp::endpoint;
using pal::net::ip::port_type;
using pal::net::ip::resolver_base;
using resolve_result = pal::result<std::span<const endpoint>>;

constexpr auto numeric = resolver_base::numeric_host | resolver_base::numeric_service;

// run_for() until every counted offload has completed back, bounded by an overall deadline: run_for()
// may return 0 spuriously (a wake whose work was already drained), so only total elapsed time
// distinguishes still-in-flight from hung.
void run_until_idle (event_loop &loop)
{
	const auto deadline = event_loop::clock::now() + 5s;
	while (loop.stats().offload_in_flight > 0)
	{
		const auto now = event_loop::clock::now();
		REQUIRE(now < deadline);
		auto r = loop.run_for(deadline - now);
		REQUIRE(r);
	}
}

TEST_CASE("async/resolver")
{
	auto loop = make_loop();
	REQUIRE(loop);

	auto pool = make_thread_pool(1);
	REQUIRE(pool);

	auto h = loop->make_handle(resolver(), *pool);
	REQUIRE(h);

	alignas(endpoint) std::array<std::byte, 4 * sizeof(endpoint)> storage{};
	task t{storage};

	SECTION("start_resolve: numeric v4")
	{
		size_t count = 0;
		endpoint first{};
		const void *data = nullptr;
		std::thread::id handler_thread{};
		task *got = nullptr;

		// clang-format off
		h->start_resolve(t.borrow(), "127.0.0.1", "7", numeric, [&] (task_ptr &&p, resolve_result &&r) noexcept
		{
			handler_thread = std::this_thread::get_id();
			got = p.get();
			if (r)
			{
				count = r->size();
				data = r->data();
				if (!r->empty())
				{
					first = (*r)[0];
				}
			}
		});
		// clang-format on

		CHECK(loop->stats().offload_in_flight == 1);
		run_until_idle(*loop);

		CHECK(handler_thread == std::this_thread::get_id());
		CHECK(got == &t);
		REQUIRE(count == 1);
		CHECK(first.address().is_v4());
		CHECK(first.address().is_loopback());
		CHECK(first.port() == port_type{7});

		// endpoints landed in the attached storage; the task's own window is untouched
		CHECK(data == static_cast<const void *>(storage.data()));
		CHECK(t.span().data() == storage.data());
		CHECK(t.span().size() == storage.size());
	}

	SECTION("start_resolve: numeric v6")
	{
		size_t count = 0;
		endpoint first{};

		// clang-format off
		h->start_resolve(t.borrow(), "::1", "7", numeric, [&] (task_ptr &&, resolve_result &&r) noexcept
		{
			if (r)
			{
				count = r->size();
				if (!r->empty())
				{
					first = (*r)[0];
				}
			}
		});
		// clang-format on

		run_until_idle(*loop);
		REQUIRE(count == 1);
		CHECK(first.address().is_v6());
		CHECK(first.address().is_loopback());
		CHECK(first.port() == port_type{7});
	}

	SECTION("start_resolve: overload without flags")
	{
		size_t count = 0;
		bool loopback = true;

		// clang-format off
		h->start_resolve(t.borrow(), "127.0.0.1", "7", [&] (task_ptr &&, resolve_result &&r) noexcept
		{
			if (r)
			{
				count = r->size();
				for (const auto &ep: *r)
				{
					loopback = loopback && ep.address().is_loopback();
				}
			}
		});
		// clang-format on

		run_until_idle(*loop);
		REQUIRE(count >= 1);
		CHECK(loopback);
	}

	SECTION("start_resolve: host not found")
	{
		std::error_code ec{};
		bool had_value = true;

		// clang-format off
		h->start_resolve(t.borrow(), "host.not.found", "7", [&] (task_ptr &&, resolve_result &&r) noexcept
		{
			had_value = r.has_value();
			if (!r)
			{
				ec = r.error();
			}
		});
		// clang-format on

		run_until_idle(*loop);
		CHECK_FALSE(had_value);
		CHECK(ec == pal::net::ip::resolver_errc::host_not_found);
	}

	SECTION("start_resolve: task reuse round trips")
	{
		std::array<port_type, 2> seen{};

		// clang-format off
		h->start_resolve(t.borrow(), "127.0.0.1", "7", numeric, [&] (task_ptr &&, resolve_result &&r) noexcept
		{
			if (r && !r->empty())
			{
				seen[0] = (*r)[0].port();
			}
		});

		run_until_idle(*loop);

		h->start_resolve(t.borrow(), "127.0.0.1", "9", numeric, [&] (task_ptr &&, resolve_result &&r) noexcept
		{
			if (r && !r->empty())
			{
				seen[1] = (*r)[0].port();
			}
		});
		// clang-format on

		run_until_idle(*loop);

		CHECK(seen[0] == port_type{7});
		CHECK(seen[1] == port_type{9});
	}

	SECTION("offload_in_flight: counted for start_resolve, not for plain post")
	{
		std::atomic<bool> release = false;
		task blocker;

		// clang-format off

		// occupy the single worker so the resolve stays queued while we observe the gauge
		pool->post(*loop, blocker.borrow(),
			[&release] (task &) noexcept
			{
				while (!release)
				{
					std::this_thread::yield();
				}
			},
			[] (task_ptr &&) noexcept {}
		);

		CHECK(loop->stats().offload_in_flight == 0);

		int called = 0;
		h->start_resolve(t.borrow(), "127.0.0.1", "7",
			[&called] (task_ptr &&, resolve_result &&) noexcept { ++called; }
		);
		CHECK(loop->stats().offload_in_flight == 1);

		// clang-format on

		release = true;
		run_until_idle(*loop);
		CHECK(called == 1);
		CHECK(loop->stats().offload_in_flight == 0);
	}

	SECTION("start_resolve: stale-generation completion discarded by app, still decremented")
	{
		struct session
		{
			uint32_t generation = 0;
		};
		session s{};
		int delivered = 0, discarded = 0;

		// clang-format off
		h->start_resolve(t.borrow(), "127.0.0.1", "7", numeric,
			[&s, &delivered, &discarded, generation = s.generation] (task_ptr &&, resolve_result &&) noexcept
			{
				if (generation != s.generation)
				{
					++discarded;
				}
				else
				{
					++delivered;
				}
			}
		);
		// clang-format on

		// app "recycles" the session while the op is in flight; the closure's generation goes stale
		++s.generation;

		run_until_idle(*loop);
		CHECK(discarded == 1);
		CHECK(delivered == 0);
		CHECK(loop->stats().offload_in_flight == 0);
	}

	SECTION("handle survives loop and pool moves")
	{
		event_loop moved_loop = std::move(*loop);
		const thread_pool moved_pool = std::move(*pool);

		size_t count = 0;

		// clang-format off
		h->start_resolve(t.borrow(), "127.0.0.1", "7", numeric, [&count] (task_ptr &&, resolve_result &&r) noexcept
		{
			count = r ? r->size() : 0;
		});
		// clang-format on

		run_until_idle(moved_loop);
		CHECK(count == 1);
	}
}

TEST_CASE("async/resolver start_resolve contracts")
{
	if constexpr (pal::build == pal::build_type::debug)
	{
		const auto handler = [] (task_ptr &&, resolve_result &&) noexcept {};

		SECTION("payload storage required")
		{
			// clang-format off
			auto msg = pal_test::require_terminate([&]
			{
				auto loop = make_loop().value();
				auto pool = make_thread_pool(1).value();
				auto h = loop.make_handle(resolver(), pool).value();
				task t;
				h.start_resolve(t.borrow(), "127.0.0.1", "7", numeric, handler);
			});
			// clang-format on

			CHECK(msg.contains("payload"));
		}

		SECTION("payload alignment required")
		{
			// clang-format off
			auto msg = pal_test::require_terminate([&]
			{
				auto loop = make_loop().value();
				auto pool = make_thread_pool(1).value();
				auto h = loop.make_handle(resolver(), pool).value();
				alignas(endpoint) std::array<std::byte, 2 * sizeof(endpoint)> storage{};
				task t{std::span{storage}.subspan(1)};
				h.start_resolve(t.borrow(), "127.0.0.1", "7", numeric, handler);
			});
			// clang-format on

			CHECK(msg.contains("misaligned"));
		}

		SECTION("canonical_name is not supported")
		{
			// clang-format off
			auto msg = pal_test::require_terminate([&]
			{
				auto loop = make_loop().value();
				auto pool = make_thread_pool(1).value();
				auto h = loop.make_handle(resolver(), pool).value();
				alignas(endpoint) std::array<std::byte, sizeof(endpoint)> storage{};
				task t{storage};
				h.start_resolve(t.borrow(), "127.0.0.1", "7", resolver_base::canonical_name, handler);
			});
			// clang-format on

			CHECK(msg.contains("canonical_name"));
		}
	}
}

TEST_CASE("async/resolver loop destructor contract")
{
	if constexpr (pal::build == pal::build_type::debug)
	{
		const auto handler = [] (task_ptr &&, resolve_result &&) noexcept {};

		// A loop destroyed with a counted offload in flight -> REQUIRE crash. The single worker is held
		// on `blocker` (posting back to the leaked `keeper`), so the resolve is still queued and the
		// dying loop's inbox is deterministically empty.
		static task blocker, pending;
		alignas(endpoint) static std::array<std::byte, sizeof(endpoint)> storage{};
		static std::atomic<bool> release = false;

		// clang-format off
		auto msg = pal_test::require_terminate([&]
		{
			auto keeper = make_loop().value();
			auto pool = make_thread_pool(1).value();
			pool.post(keeper, blocker.borrow(),
				[] (task &) noexcept
				{
					while (!release)
					{
						std::this_thread::yield();
					}
				},
				[] (task_ptr &&) noexcept {}
			);

			auto loop = make_loop().value();
			auto h = loop.make_handle(resolver(), pool).value();
			pending.span(storage);
			h.start_resolve(pending.borrow(), "127.0.0.1", "7", numeric, handler);
		});
		// clang-format on

		// unblock the leaked worker (the trapped terminate leaked the pool without joining it)
		release = true;
		CHECK(msg.contains("offload"));
	}
}

} // namespace
