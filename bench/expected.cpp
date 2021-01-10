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


struct exception: public bench_base //{{{1
{
	using bench_base::bench_base;

	time_t func ()
	{
		if (code_path == errc::success)
		{
			return time(nullptr);
		}
		throw std::exception{};
	}

	time_t run ()
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


struct expected: public bench_base //{{{1
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
		return func().value_or(time_t{});
	}
};


struct expected_throw: public bench_base //{{{1
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
BENCHMARK_CAPTURE(returns, expected_success, expected{errc::success});
BENCHMARK_CAPTURE(returns, expected_throw_success, expected_throw{errc::success});

BENCHMARK_CAPTURE(returns, value_failure, value{errc::failure});
BENCHMARK_CAPTURE(returns, exception_failure, exception{errc::failure});
BENCHMARK_CAPTURE(returns, expected_failure, expected{errc::failure});
BENCHMARK_CAPTURE(returns, expected_throw_failure, expected_throw{errc::failure});


} // namespace
