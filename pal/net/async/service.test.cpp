#include <pal/net/async/service>
#include <pal/net/test>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>


namespace {


//
// Because OS poller may return early/late, this test is flaky
//

TEST_CASE("net/async/service", "[!mayfail]")
{
	using namespace std::chrono_literals;
	using Catch::Approx;

	SECTION("make_service not enough memory")
	{
		pal_test::bad_alloc_once x;
		auto make_service = pal::net::async::make_service();
		REQUIRE_FALSE(make_service);
		CHECK(make_service.error() == std::errc::not_enough_memory);
	}

	pal::net::async::request request, *request_ptr = nullptr;
	auto process = [&request_ptr](auto *request)
	{
		request_ptr = request;
	};
	auto service = pal_try(pal::net::async::make_service());

	SECTION("assign")
	{
		// meaningless test, just to increase coverage
		auto s = pal_try(pal::net::async::make_service());
		s = std::move(service);
	}

	SECTION("post")
	{
		service.post(&request);
		service.run_once(process);
		CHECK(request_ptr == &request);
	}

	SECTION("post_after")
	{
		constexpr auto margin = 10ms;
		auto expected_runtime = 0ms;
		auto start = service.now();

		SECTION("run_once: post immediately")
		{
			service.post_after(0ms, &request);
			service.run_once(process);
			CHECK(request_ptr == &request);
		}

		SECTION("run_once: no post")
		{
			service.post_after(100ms, &request);
			service.run_once(process);
			CHECK(request_ptr == nullptr);
		}

		SECTION("run_for: post immediately")
		{
			service.post_after(0ms, &request);
			service.run_for(100ms, process);
			CHECK(request_ptr == &request);
		}

		SECTION("run_for: post before")
		{
			service.post_after(100ms, &request);
			service.run_for(200ms, process);
			CHECK(request_ptr == &request);
			expected_runtime = 100ms;
		}

		SECTION("run_for: post at")
		{
			service.post_after(100ms, &request);
			service.run_for(100ms, process);
			CHECK(request_ptr == &request);
			expected_runtime = 100ms;
		}

		SECTION("run_for: post after")
		{
			service.post_after(200ms, &request);
			service.run_for(100ms, process);
			CHECK(request_ptr == nullptr);
			service.run_for(100ms, process);
			CHECK(request_ptr == &request);
			expected_runtime = 200ms;
		}

		SECTION("run: post immediately")
		{
			service.post_after(0ms, &request);
			service.run(process);
			CHECK(request_ptr == &request);
		}

		SECTION("run: post at")
		{
			service.post_after(100ms, &request);
			service.run(process);
			CHECK(request_ptr == &request);
			expected_runtime = 100ms;
		}

		SECTION("run_for: post multiple")
		{
			std::array<pal::net::async::request, 3> requests;
			service.post_after(50ms, &requests[0]);
			service.post_after(20ms, &requests[1]);
			service.post_after(70ms, &requests[2]);

			service.run_for(100ms, process);
			CHECK(request_ptr == &requests[1]);

			service.run_for(100ms, process);
			CHECK(request_ptr == &requests[0]);

			service.run_for(100ms, process);
			CHECK(request_ptr == &requests[2]);

			expected_runtime = 70ms;
		}

		auto runtime = std::chrono::duration_cast<decltype(expected_runtime)>(service.now() - start);
		CHECK(runtime.count() == Approx(expected_runtime.count()).margin(margin.count()));
	}
}


} // namespace
