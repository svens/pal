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


struct value: public bench_base
{
	using bench_base::bench_base;

	errc func (time_t &result)
	{
		if (code_path == errc::success)
		{
			result = time(nullptr);
			return errc::success;
		}
		return errc::failure;
	}

	time_t run ()
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


struct exception: public bench_base
{
	using bench_base::bench_base;

	time_t func ()
	{
		if (code_path == errc::success)
		{
			return time(nullptr);
		}
		throw errc::failure;
	}

	time_t run ()
	{
		try
		{
			auto result = func();
			benchmark::DoNotOptimize(result);
			return result;
		}
		catch (errc e)
		{
			benchmark::DoNotOptimize(e);
			return {};
		}
	}
};


struct expected: public bench_base
{
	using bench_base::bench_base;

	pal::expected<time_t, errc> func ()
	{
		if (code_path == errc::success)
		{
			return time(nullptr);
		}
		return pal::unexpected{errc::failure};
	}

	time_t run ()
	{
		if (auto r = func())
		{
			return *r;
		}
		else
		{
			benchmark::DoNotOptimize(r.error());
			return {};
		}
	}
};


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
BENCHMARK_CAPTURE(returns, expected_success, expected{errc::success});

BENCHMARK_CAPTURE(returns, value_failure, value{errc::failure});
BENCHMARK_CAPTURE(returns, exception_failure, exception{errc::failure});
BENCHMARK_CAPTURE(returns, expected_failure, expected{errc::failure});


} // namespace
