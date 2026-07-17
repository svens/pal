#include <pal/async/__async.hpp>
#include <pal/test.hpp>
#include <catch2/catch_test_macros.hpp>
#include <string>

namespace
{

using namespace pal::async;

// Minimal carrier + op stand-ins for exercising __async::completion in isolation,
// ahead of pal/async/task.hpp (see task.test.cpp for the task-integrated tests,
// including the move-only task_ptr threading through the erased call).
struct carrier
{
	__async::completion<carrier> completion;
	int value = 0;
};

struct op_test
{
	using signature = void(int) noexcept;

	template <typename F>
	static void dispatch (carrier &c, F &f) noexcept
	{
		f(c.value);
	}
};

TEST_CASE("async/__async")
{
	// clang-format off

	carrier c;

	SECTION("single-shot: bind and complete invokes the closure once")
	{
		int calls = 0, seen_value = -1;
		c.completion.bind<op_test>([&](int v) noexcept
		{
			++calls;
			seen_value = v;
		});
		c.value = 5;
		c.completion.complete(c);
		CHECK(calls == 1);
		CHECK(seen_value == 5);
	}

	SECTION("single-shot: rebind inside own callback is reused on the next complete")
	{
		int first_calls = 0, second_calls = 0;

		c.completion.bind<op_test>([&](int) noexcept
		{
			++first_calls;
			c.completion.bind<op_test>([&second_calls](int) noexcept
			{
				++second_calls;
			});
		});

		c.completion.complete(c);
		CHECK(first_calls == 1);
		CHECK(second_calls == 0);

		c.completion.complete(c);
		CHECK(second_calls == 1);
	}

	SECTION("single-shot: double complete without rebind is a REQUIRE violation")
	{
		if constexpr (pal::build == pal::build_type::debug)
		{
			c.completion.bind<op_test>([](int) noexcept {});
			c.completion.complete(c);

			auto msg = pal_test::require_terminate([&] { c.completion.complete(c); });
			CHECK(msg.contains("unbound"));
		}
	}

	SECTION("single-shot: bind while already bound is a REQUIRE violation")
	{
		if constexpr (pal::build == pal::build_type::debug)
		{
			c.completion.bind<op_test>([](int) noexcept {});

			auto msg = pal_test::require_terminate([&] { c.completion.bind<op_test>([](int) noexcept {}); });
			CHECK(msg.contains("bound"));

			c.completion.complete(c);
		}
	}

	SECTION("multishot: arm invokes the closure in place across multiple completions")
	{
		int calls = 0;
		c.completion.arm<op_test>([&calls](int) noexcept { ++calls; });
		CHECK(c.completion.armed());

		c.completion.complete(c);
		c.completion.complete(c);
		CHECK(calls == 2);
		CHECK(c.completion.armed());

		c.completion.stop();
		CHECK_FALSE(c.completion.armed());
	}

	SECTION("multishot: rebind while armed is a REQUIRE violation")
	{
		if constexpr (pal::build == pal::build_type::debug)
		{
			c.completion.arm<op_test>([](int) noexcept {});

			auto msg = pal_test::require_terminate([&] { c.completion.arm<op_test>([](int) noexcept {}); });
			CHECK(msg.contains("armed"));

			c.completion.stop();
		}
	}

	SECTION("stop while not armed is a REQUIRE violation")
	{
		if constexpr (pal::build == pal::build_type::debug)
		{
			auto msg = pal_test::require_terminate([&] { c.completion.stop(); });
			CHECK(msg.contains("armed"));
		}
	}

	// clang-format on
}

// --- handler concept: accepted and rejected closure shapes ----------------------

static_assert(__async::handler<decltype([] (int) noexcept {}), op_test::signature>);

[[maybe_unused]] void capturing_lambda_is_a_handler (int &state)
{
	static_assert(__async::handler<decltype([&state] (int) noexcept { ++state; }), op_test::signature>);
	static_assert(__async::handler<decltype([p = &state] (int) noexcept { ++*p; }), op_test::signature>);
}

struct no_default_ctor
{
	explicit no_default_ctor (int) noexcept
	{
	}
	void operator() (int) const noexcept
	{
	}
};
static_assert(__async::handler<no_default_ctor, op_test::signature>);

struct not_trivially_copyable
{
	std::string s;
	void operator() (int) const noexcept
	{
	}
};
static_assert(!__async::handler<not_trivially_copyable, op_test::signature>);

struct too_big
{
	std::byte padding[__async::closure_capacity + 1];
	void operator() (int) const noexcept
	{
	}
};
static_assert(!__async::handler<too_big, op_test::signature>);

struct over_aligned
{
	alignas(alignof(std::max_align_t) * 2) std::byte b;
	void operator() (int) const noexcept
	{
	}
};
static_assert(!__async::handler<over_aligned, op_test::signature>);

struct wrong_signature
{
	void operator() () const noexcept
	{
	}
};
static_assert(!__async::handler<wrong_signature, op_test::signature>);

struct throwing_call
{
	void operator() (int) const
	{
	}
};
static_assert(!__async::handler<throwing_call, op_test::signature>);

} // namespace
