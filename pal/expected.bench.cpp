#include <pal/expected>
#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_template_test_macros.hpp>


namespace {


enum class errc
{
	failure,
	success,
};


struct bench_base
{
	errc code_path;

	bench_base (errc code_path) noexcept
		: code_path{code_path}
	{ }
};


struct value: bench_base //{{{1
{
	using bench_base::bench_base;

	errc func (time_t &result) noexcept
	{
		result = time(nullptr);
		return code_path;
	}

	time_t run () noexcept
	{
		time_t result;
		switch (func(result))
		{
			case errc::success:
				return result;
			default:
				return {};
		}
	}
};


struct exception: bench_base //{{{1
{
	using bench_base::bench_base;

	time_t func ()
	{
		auto result = time(nullptr);
		if (code_path == errc::failure)
		{
			throw std::runtime_error{"exception"};
		}
		return result;
	}

	time_t run () noexcept
	{
		try
		{
			return func();
		}
		catch (const std::exception &e)
		{
			// pretend examining error
			// (not sure if compiler optimises this out)
			if (*e.what() != 'x')
			{
				return 1;
			}
			return {};
		}
	}
};


struct variant: bench_base //{{{1
{
	using bench_base::bench_base;

	std::variant<time_t, errc> func () noexcept
	{
		auto result = time(nullptr);
		if (code_path == errc::success)
		{
			return result;
		}
		return errc::failure;
	}

	time_t run () noexcept
	{
		auto result = func();
		if (auto pt = std::get_if<time_t>(&result))
		{
			return *pt;
		}
		return time_t{};
	}
};


struct expected: bench_base //{{{1
{
	using bench_base::bench_base;

	pal::expected<time_t, errc> func () noexcept
	{
		auto result = time(nullptr);
		if (code_path == errc::success)
		{
			return result;
		}
		return pal::unexpected{errc::failure};
	}

	time_t run () noexcept
	{
		return func().value_or(time_t{});
	}
};


struct expected_throw: expected //{{{1
{
	using expected::expected;

	time_t run () noexcept
	{
		try
		{
			return func().value();
		}
		catch (const pal::bad_expected_access<errc> &e)
		{
			// pretend examining error
			// (not sure if compiler optimises this out)
			if (e.error() == errc::failure)
			{
				return 1;
			}
			return {};
		}

	}
};

//}}}1


TEMPLATE_TEST_CASE("returns", "[!benchmark]",
	value,
	exception,
	variant,
	expected,
	expected_throw)
{
	BENCHMARK_ADVANCED("success")(Catch::Benchmark::Chronometer meter)
	{
		TestType f{errc::success};
		meter.measure([&f]{ return f.run(); });
	};

	BENCHMARK_ADVANCED("failure")(Catch::Benchmark::Chronometer meter)
	{
		TestType f{errc::failure};
		meter.measure([&f]{ return f.run(); });
	};
}


} // namespace
