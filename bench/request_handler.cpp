#include <benchmark/benchmark.h>
#include <pal/net/async/request>
#include <vector>


//
// Asynchronous networking requests passing/handling library -> application
//
// Possible approaches:
// 1) library gathers per poll() completions into queue and passes it to
//    application callback
// 2) library invokes application callback with completions immediately while
//    iterating events from poll()
//
// Note: in this context, poll() is common name for platform dependent syscall
// (epoll_wait, kevent, GetQueuedCompletionStatusEx)
//


namespace {

//
// (1)
//

template <typename Handler>
struct request_queue_handler
{
	Handler h;

	request_queue_handler (Handler h)
		: h{h}
	{ }

	void handle (pal::net::async::request_queue &&queue)
	{
		h(std::move(queue));
	}
};

void request_queue_handle (benchmark::State &state)
{
	request_queue_handler handler(
		[](pal::net::async::request_queue &&queue)
		{
			while (auto *request = queue.try_pop())
			{
				benchmark::DoNotOptimize(
					std::get_if<pal::net::async::send_to>(request)
				);
			}
		}
	);

	std::vector<pal::net::async::request> pool(state.range(0));
	pal::net::async::request_queue queue;

	for (auto _: state)
	{
		for (auto &r: pool)
		{
			queue.push(&r);
		}
		handler.handle(std::move(queue));
	}
}
BENCHMARK(request_queue_handle)
	->RangeMultiplier(2)
	->Range(8, 2<<9)
;

//
// (2)
//

template <typename Handler>
struct request_handler
{
	Handler h;

	request_handler (Handler h)
		: h{h}
	{ }

	static void invoke (request_handler *self, pal::net::async::request *request)
	{
		self->h(request);
	}
};

void request_handle (benchmark::State &state)
{
	request_handler handler(
		[](pal::net::async::request *request)
		{
			benchmark::DoNotOptimize(
				std::get_if<pal::net::async::send_to>(request)
			);
		}
	);

	std::vector<pal::net::async::request> pool(state.range(0));
	auto handle = decltype(handler)::invoke;

	for (auto _: state)
	{
		for (auto &r: pool)
		{
			handle(&handler, &r);
		}
	}
}
BENCHMARK(request_handle)
	->RangeMultiplier(2)
	->Range(8, 2<<9)
;


} // namespace
