#include <benchmark/benchmark.h>
#include <pal/expected>


namespace {


enum class errc
{
	failure,
	success,
};


struct bench_base
{
	errc code_path;

	bench_base (errc code_path)
		: code_path{code_path}
	{}
};


struct value: public bench_base //{{{1
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


struct exception: public bench_base //{{{1
{
	using bench_base::bench_base;

	time_t func ()
	{
		auto result = time(nullptr);
		if (code_path == errc::failure)
		{
			throw std::exception{};
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
			benchmark::DoNotOptimize(e.what());
			return {};
		}
	}
};


struct variant: public bench_base //{{{1
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


struct expected: public bench_base //{{{1
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


struct expected_throw: public expected //{{{1
{
	using expected::expected;

	time_t run ()
	{
		try
		{
			return func().value();
		}
		catch (const pal::bad_expected_access<errc> &e)
		{
			benchmark::DoNotOptimize(e.error());
			return {};
		}
	}
};

//}}}1


template <typename F>
void returns (benchmark::State &state, F f)
{
	for (auto _: state)
	{
		auto result = f.run();
		benchmark::DoNotOptimize(result);
	}
}

BENCHMARK_CAPTURE(returns, value_success, value{errc::success});
BENCHMARK_CAPTURE(returns, exception_success, exception{errc::success});
BENCHMARK_CAPTURE(returns, variant_success, variant{errc::success});
BENCHMARK_CAPTURE(returns, expected_success, expected{errc::success});
BENCHMARK_CAPTURE(returns, expected_throw_success, expected_throw{errc::success});

BENCHMARK_CAPTURE(returns, value_failure, value{errc::failure});
BENCHMARK_CAPTURE(returns, exception_failure, exception{errc::failure});
BENCHMARK_CAPTURE(returns, variant_failure, variant{errc::failure});
BENCHMARK_CAPTURE(returns, expected_failure, expected{errc::failure});
BENCHMARK_CAPTURE(returns, expected_throw_failure, expected_throw{errc::failure});


} // namespace
