#include <pal/async/task_pool.hpp>
#include <pal/test.hpp>
#include <catch2/catch_test_macros.hpp>
#include <system_error>
#include <tuple>
#include <utility>

namespace
{

using namespace pal::async;

// Op whose handler takes ownership of the carrier, reconstructed from the bare
// pointer like a library completion path does (borrow() is off-limits for
// pool-managed tasks).
struct op_recycle
{
	using signature = void(task_ptr &&) noexcept;

	template <typename F>
	static void dispatch (task &t, F &f, std::error_code, size_t) noexcept
	{
		f(task_ptr{&t});
	}
};

TEST_CASE("async/task_pool")
{
	constexpr auto default_buffer_size = 2048 - sizeof(task);
	task_pool<2> pool;

	SECTION("try_acquire() yields a task with the full default buffer attached")
	{
		auto t = pool.try_acquire();
		REQUIRE(t != nullptr);
		CHECK(t->span().size() == default_buffer_size);
	}

	SECTION("distinct tasks with distinct buffers until exhaustion, then empty")
	{
		auto a = pool.try_acquire();
		auto b = pool.try_acquire();
		REQUIRE(a != nullptr);
		REQUIRE(b != nullptr);
		CHECK(a.get() != b.get());
		CHECK(a->span().data() != b->span().data());

		auto c = pool.try_acquire();
		CHECK(c == nullptr);

		// recycling one makes it available again
		a = nullptr;
		c = pool.try_acquire();
		CHECK(c != nullptr);
	}

	SECTION("LIFO reuse: most recently recycled task first")
	{
		auto a = pool.try_acquire();
		auto b = pool.try_acquire();
		auto *first = a.get(), *second = b.get();
		a = nullptr;
		b = nullptr;

		auto c = pool.try_acquire();
		auto d = pool.try_acquire();
		CHECK(c.get() == second);
		CHECK(d.get() == first);
	}

	SECTION("recycle resets the payload window to the slot's full buffer")
	{
		auto t = pool.try_acquire();
		auto *carrier = t.get();
		const auto *data = t->span().data();

		t->span(t->span().subspan(2, 4));
		t = nullptr;

		t = pool.try_acquire();
		REQUIRE(t.get() == carrier);
		CHECK(t->span().data() == data);
		CHECK(t->span().size() == default_buffer_size);
	}

	SECTION("drop inside own completion recycles back to the pool")
	{
		task &carrier = *pool.try_acquire().release();
		int calls = 0;
		// clang-format off
		carrier.bind<op_recycle>([&calls] (task_ptr &&p) noexcept
		{
			++calls;
			const auto own = std::move(p);
		});
		// clang-format on
		carrier.complete({}, 0);
		CHECK(calls == 1);

		auto t = pool.try_acquire();
		CHECK(t.get() == &carrier);
	}

	SECTION("custom BufferSize")
	{
		task_pool<1, 8> small;
		auto t = small.try_acquire();
		REQUIRE(t != nullptr);
		CHECK(t->span().size() == 8);
	}

	SECTION("borrow() on a pool task is a REQUIRE violation")
	{
		if constexpr (pal::build == pal::build_type::debug)
		{
			auto t = pool.try_acquire();
			auto msg = pal_test::require_terminate([&] { std::ignore = t->borrow(); });
			CHECK(msg.contains("pool-managed"));
		}
	}

	SECTION("destroying the pool with tasks in flight is a REQUIRE violation")
	{
		if constexpr (pal::build == pal::build_type::debug)
		{
			// clang-format off
			auto msg = pal_test::require_terminate([]
			{
				task_ptr t;
				task_pool<1> inner;
				t = inner.try_acquire();
			});
			// clang-format on
			CHECK(msg.contains("in flight"));
		}
	}
}

} // namespace
