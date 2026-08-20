#include <pal/version.hpp>

#if __pal_os_linux

#include <catch2/catch_test_case_info.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>
#include <chrono>
#include <thread>

namespace
{

using namespace std::chrono_literals;

// Closing an io_uring ring leaves the kernel finishing teardown asynchronously:
// - It then notifies this thread, cutting short whatever timed socket wait is in flight with EINTR.
// - Test order is randomised, so the wait it interrupts is any later pal/net timeout case.
// - Waiting here consumes the notification: a sleep takes it at the syscall boundary and resumes,
//   which is why it never surfaces as an error.
//
// Measured:
// - 20ms left 5 suite runs in 10 failing, 100ms none in 10 but 2 in the ~20 runs after that.
// - Rings holding a request at close settle well behind the ~10ms a ring that never submitted anything takes.
constexpr auto teardown_window = 200ms;

struct settle_listener: Catch::EventListenerBase
{
	using Catch::EventListenerBase::EventListenerBase;

	void testCaseEnded (const Catch::TestCaseStats &stats) override
	{
		if (stats.testInfo->name.starts_with("async/"))
		{
			std::this_thread::sleep_for(teardown_window);
		}
	}
};

} // namespace

CATCH_REGISTER_LISTENER(settle_listener)

#endif // __pal_os_linux
