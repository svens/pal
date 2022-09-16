#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>


namespace {

const auto unoptimize_v = static_cast<size_t>(time(nullptr));


size_t function (int i) noexcept
{
	return unoptimize_v * i;
}


struct method
{
	size_t run (int i) const noexcept
	{
		return unoptimize_v * i;
	}

	static size_t static_run (int i) noexcept
	{
		return unoptimize_v * i;
	}
};


struct virtual_method
{
	virtual ~virtual_method () noexcept = default;
	virtual size_t run (int i) const noexcept = 0;
};


template <typename F>
struct virtual_method_with_lambda: virtual_method
{
	F f_;

	virtual_method_with_lambda (F f) noexcept
		: f_{f}
	{ }

	size_t run (int i) const noexcept final override
	{
		return f_(i);
	}
};


TEST_CASE("invoke", "[!benchmark]")
{
	BENCHMARK("lambda", i)
	{
		return unoptimize_v * i;
	};

	BENCHMARK_ADVANCED("method")(Catch::Benchmark::Chronometer meter)
	{
		method f;
		meter.measure([&f](int i) { return f.run(i); });
	};

	BENCHMARK_ADVANCED("method pointer")(Catch::Benchmark::Chronometer meter)
	{
		method f;
		auto method_ptr = &method::run;
		meter.measure([&f, &method_ptr](int i) { return (f.*method_ptr)(i); });
	};

	BENCHMARK_ADVANCED("static method")(Catch::Benchmark::Chronometer meter)
	{
		meter.measure([](int i) { return method::static_run(i); });
	};

	BENCHMARK_ADVANCED("virtual method")(Catch::Benchmark::Chronometer meter)
	{
		struct virtual_method_impl: virtual_method
		{
			size_t run (int i) const noexcept final override
			{
				return unoptimize_v * i;
			}
		} impl;

		virtual_method &f = impl;
		meter.measure([&f](int i) { return f.run(i); });
	};

	BENCHMARK_ADVANCED("virtual method with lambda")(Catch::Benchmark::Chronometer meter)
	{
		auto lambda = [](int i) noexcept
		{
			return unoptimize_v * i;
		};
		virtual_method_with_lambda<decltype(lambda)> impl{lambda};
		virtual_method &f = impl;

		meter.measure([&f](int i) { return f.run(i); });
	};

	BENCHMARK_ADVANCED("function pointer")(Catch::Benchmark::Chronometer meter)
	{
		auto f = &function;
		meter.measure([&f](int i) { return (*f)(i); });
	};

	BENCHMARK_ADVANCED("std::function")(Catch::Benchmark::Chronometer meter)
	{
		std::function<size_t(int)> f = [](int i)
		{
			return unoptimize_v * i;
		};
		meter.measure([&f](int i) { return f(i); });
	};

	BENCHMARK_ADVANCED("std::reference_wrapper function")(Catch::Benchmark::Chronometer meter)
	{
		std::reference_wrapper f = std::ref(function);
		meter.measure([&f](int i) { return f(i); });
	};

	BENCHMARK_ADVANCED("std::reference_wrapper lambda")(Catch::Benchmark::Chronometer meter)
	{
		auto lambda = [](int i) noexcept
		{
			return unoptimize_v * i;
		};
		std::reference_wrapper f = std::ref(lambda);
		meter.measure([&f](int i) { return f(i); });
	};
}


} // namespace
