#include <benchmark/benchmark.h>
#include <ctime>
#include <functional>


namespace {


auto r = static_cast<size_t>(time(nullptr));


void invoke_lambda (benchmark::State &state)
{
	auto f = [&]()
	{
		return r * state.iterations();
	};

	for (auto _: state)
	{
		benchmark::DoNotOptimize(f());
	}
}
BENCHMARK(invoke_lambda);


struct method
{
	benchmark::State &state_;

	method (benchmark::State &state)
		: state_(state)
	{ }

	size_t run () const noexcept
	{
		return r * state_.iterations();
	}

	static size_t static_run (benchmark::State &state) noexcept
	{
		return r * state.iterations();
	}
};


void invoke_method (benchmark::State &state)
{
	method f{state};
	for (auto _: state)
	{
		benchmark::DoNotOptimize(f.run());
	}
}
BENCHMARK(invoke_method);


void invoke_method_ptr (benchmark::State &state)
{
	method f{state};
	auto method_ptr = &method::run;
	for (auto _: state)
	{
		benchmark::DoNotOptimize((f.*method_ptr)());
	}
}
BENCHMARK(invoke_method_ptr);


void invoke_static_method (benchmark::State &state)
{
	for (auto _: state)
	{
		benchmark::DoNotOptimize(method::static_run(state));
	}
}
BENCHMARK(invoke_static_method);


struct virtual_method
{
	virtual ~virtual_method () noexcept = default;
	virtual size_t run () const noexcept = 0;
};


void invoke_virtual_method (benchmark::State &state)
{
	struct virtual_method_override
		: public virtual_method
	{
		benchmark::State &state_;

		virtual_method_override (benchmark::State &state)
			: state_(state)
		{ }

		size_t run () const noexcept final override
		{
			return r * state_.iterations();
		}
	} impl{state};

	virtual_method &f = impl;
	for (auto _: state)
	{
		benchmark::DoNotOptimize(f.run());
	}
}
BENCHMARK(invoke_virtual_method);


template <typename F>
struct virtual_method_with_callable
	: public virtual_method
{
	F fn_;
	benchmark::State &state_;

	virtual_method_with_callable (F fn, benchmark::State &state) noexcept
		: fn_(fn)
		, state_(state)
	{ }

	size_t run () const noexcept final override
	{
		return fn_(state_);
	}
};


void invoke_virtual_method_with_lambda (benchmark::State &state)
{
	auto lambda = [](benchmark::State &state) noexcept
	{
		return r * state.iterations();
	};
	virtual_method_with_callable<decltype(lambda)> impl{lambda, state};
	virtual_method &f = impl;

	for (auto _: state)
	{
		benchmark::DoNotOptimize(f.run());
	}
}
BENCHMARK(invoke_virtual_method_with_lambda);


size_t function (benchmark::State &state) noexcept
{
	return r * state.iterations();
}


void invoke_function_ptr (benchmark::State &state)
{
	auto fp = &function;
	for (auto _: state)
	{
		benchmark::DoNotOptimize((*fp)(state));
	}
}
BENCHMARK(invoke_function_ptr);


void invoke_std_function (benchmark::State &state)
{
	std::function<size_t(benchmark::State &)> f = &function;
	for (auto _: state)
	{
		benchmark::DoNotOptimize(f(state));
	}
}
BENCHMARK(invoke_std_function);


} // namespace
