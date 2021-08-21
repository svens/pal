#include <pal/net/async/service>
#include <pal/net/test>


namespace {


TEST_CASE("net/async/service", "[!mayfail]")
{
	using namespace std::chrono_literals;

	SECTION("make_service not enough memory")
	{
		pal_test::bad_alloc_once x;
		auto make_service = pal::net::async::make_service([](auto){});
		REQUIRE_FALSE(make_service);
		CHECK(make_service.error() == std::errc::not_enough_memory);
	}

	pal::net::async::request request, *request_ptr = nullptr;
	auto service = pal::net::async::make_service(
		[&](auto &&queue)
		{
			request_ptr = queue.try_pop();
			REQUIRE(queue.empty());
		}
	).value();

	SECTION("post")
	{
		service.post(&request);
		service.run_once();
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
			service.run_once();
			CHECK(request_ptr == &request);
		}

		SECTION("run_once: no post")
		{
			service.post_after(100ms, &request);
			service.run_once();
			CHECK(request_ptr == nullptr);
		}

		SECTION("run_for: post immediately")
		{
			service.post_after(0ms, &request);
			service.run_for(100ms);
			CHECK(request_ptr == &request);
		}

		SECTION("run_for: post before")
		{
			service.post_after(100ms, &request);
			service.run_for(200ms);
			CHECK(request_ptr == &request);
			expected_runtime = 100ms;
		}

		SECTION("run_for: post at")
		{
			service.post_after(100ms, &request);
			service.run_for(100ms);
			CHECK(request_ptr == &request);
			expected_runtime = 100ms;
		}

		SECTION("run_for: post after")
		{
			service.post_after(200ms, &request);
			service.run_for(100ms);
			CHECK(request_ptr == nullptr);
			service.run_for(100ms);
			CHECK(request_ptr == &request);
			expected_runtime = 200ms;
		}

		SECTION("run: post immediately")
		{
			service.post_after(0ms, &request);
			service.run();
			CHECK(request_ptr == &request);
		}

		SECTION("run: post at")
		{
			service.post_after(100ms, &request);
			service.run();
			CHECK(request_ptr == &request);
			expected_runtime = 100ms;
		}

		SECTION("run_for: post multiple")
		{
			std::array<pal::net::async::request, 3> requests;
			service.post_after(50ms, &requests[0]);
			service.post_after(20ms, &requests[1]);
			service.post_after(70ms, &requests[2]);

			service.run_for(100ms);
			CHECK(request_ptr == &requests[1]);

			service.run_for(100ms);
			CHECK(request_ptr == &requests[0]);

			service.run_for(100ms);
			CHECK(request_ptr == &requests[2]);

			expected_runtime = 70ms;
		}

		auto runtime = std::chrono::duration_cast<decltype(expected_runtime)>(service.now() - start);
		CHECK(runtime.count() == Approx(expected_runtime.count()).margin(margin.count()));
	}
}


} // namespace
