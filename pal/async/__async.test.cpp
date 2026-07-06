#include <pal/async/__async.hpp>
#include <pal/test.hpp>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <system_error>

namespace
{

using namespace pal::async;

// Minimal carrier + op stand-ins for exercising __async::completion in isolation,
// ahead of pal/async/task.hpp (see task.test.cpp for the task-integrated tests,
// including the move-only task_ptr threading through the erased call).
struct carrier
{
	__async::completion<carrier> completion;
};

struct op_test
{
	using signature = void(int, std::error_code) noexcept;

	template <typename F>
	static void dispatch (carrier &, F &f, std::error_code ec, size_t n) noexcept
	{
		f(static_cast<int>(n), ec);
	}
};

// Completion handlers must be default constructible (for the single-shot
// copy-out local) and trivially copyable/destructible, which rules out
// capturing lambdas; state is threaded through pointer members instead.
struct record_call
{
	int *calls;
	int *seen_value;
	std::error_code *seen_ec;

	void operator() (int v, std::error_code ec) const noexcept
	{
		++*calls;
		if (seen_value != nullptr)
		{
			*seen_value = v;
		}
		if (seen_ec != nullptr)
		{
			*seen_ec = ec;
		}
	}
};

struct noop
{
	void operator() (int, std::error_code) const noexcept
	{
	}
};

TEST_CASE("async/__async")
{
	// clang-format off

	carrier c;

	SECTION("single-shot: bind and complete invokes the closure once")
	{
		int calls = 0, seen_value = -1;
		std::error_code seen_ec = std::make_error_code(std::errc::io_error);
		c.completion.bind<op_test>(record_call
		{
			.calls = &calls,
			.seen_value = &seen_value,
			.seen_ec = &seen_ec
		});
		c.completion.complete(c, {}, 5);
		CHECK(calls == 1);
		CHECK(seen_value == 5);
		CHECK_FALSE(seen_ec);
	}

	SECTION("single-shot: rebind inside own callback is reused on the next complete")
	{
		int first_calls = 0, second_calls = 0;

		struct rebind_on_call
		{
			carrier *c;
			int *first_calls;
			int *second_calls;

			void operator() (int, std::error_code) const noexcept
			{
				++*first_calls;
				c->completion.bind<op_test>(record_call
				{
					.calls = second_calls,
					.seen_value = nullptr,
					.seen_ec = nullptr
				});
			}
		};

		c.completion.bind<op_test>(rebind_on_call
		{
			.c = &c,
			.first_calls = &first_calls,
			.second_calls = &second_calls
		});

		c.completion.complete(c, {}, 0);
		CHECK(first_calls == 1);
		CHECK(second_calls == 0);

		c.completion.complete(c, {}, 0);
		CHECK(second_calls == 1);
	}

	SECTION("single-shot: double complete without rebind is a REQUIRE violation")
	{
		if constexpr (pal::build == pal::build_type::debug)
		{
			c.completion.bind<op_test>(noop{});
			c.completion.complete(c, {}, 0);

			auto msg = pal_test::require_terminate([&] { c.completion.complete(c, {}, 0); });
			CHECK(msg.contains("unbound"));
		}
	}

	SECTION("multishot: arm invokes the closure in place across multiple completions")
	{
		int calls = 0;
		c.completion.arm<op_test>(record_call{.calls = &calls, .seen_value = nullptr, .seen_ec = nullptr});
		CHECK(c.completion.armed());

		c.completion.complete(c, {}, 0);
		c.completion.complete(c, {}, 0);
		CHECK(calls == 2);
		CHECK(c.completion.armed());

		c.completion.stop();
		CHECK_FALSE(c.completion.armed());
	}

	SECTION("multishot: rebind while armed is a REQUIRE violation")
	{
		if constexpr (pal::build == pal::build_type::debug)
		{
			c.completion.arm<op_test>(noop{});

			auto msg = pal_test::require_terminate([&] { c.completion.arm<op_test>(noop{}); });
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

// --- constraint violations as compile-fail cases -------------------------------

struct not_trivially_copyable
{
	std::string s;
	void operator() (int, std::error_code) const noexcept
	{
	}
};
static_assert(!__async::handler<not_trivially_copyable, op_test::signature>);

struct not_default_constructible
{
	explicit not_default_constructible (int) noexcept
	{
	}
	void operator() (int, std::error_code) const noexcept
	{
	}
};
static_assert(!__async::handler<not_default_constructible, op_test::signature>);

struct too_big
{
	std::byte padding[__async::closure_capacity + 1];
	void operator() (int, std::error_code) const noexcept
	{
	}
};
static_assert(!__async::handler<too_big, op_test::signature>);

struct over_aligned
{
	alignas(alignof(std::max_align_t) * 2) std::byte b;
	void operator() (int, std::error_code) const noexcept
	{
	}
};
static_assert(!__async::handler<over_aligned, op_test::signature>);

struct wrong_signature
{
	void operator() (int) const noexcept
	{
	}
};
static_assert(!__async::handler<wrong_signature, op_test::signature>);

struct throwing_call
{
	void operator() (int, std::error_code) const
	{
	}
};
static_assert(!__async::handler<throwing_call, op_test::signature>);

static_assert(__async::handler<noop, op_test::signature>);

} // namespace
