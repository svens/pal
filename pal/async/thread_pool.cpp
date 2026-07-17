#include <pal/async/thread_pool.hpp>
#include <pal/require.hpp>
#include <condition_variable>
#include <mutex>
#include <new>
#include <system_error>
#include <thread>
#include <vector>

namespace pal::async
{

namespace __thread_pool
{

struct impl_type
{
	std::mutex mutex{};
	std::condition_variable pending{};
	__task::attorney::task_queue queue{};
	std::vector<std::thread> workers{};
	bool stop = false;

	~impl_type () noexcept;

	void run () noexcept;
};

impl_type::~impl_type () noexcept
{
	{
		const std::scoped_lock lock{mutex};
		pal_require(queue.empty(), "thread_pool destroyed with a pending queue");
		stop = true;
	}
	pending.notify_all();

	for (auto &worker: workers)
	{
		worker.join();
	}
}

void impl_type::run () noexcept
{
	std::unique_lock lock{mutex};
	for (;;)
	{
		if (task *t = queue.try_pop())
		{
			lock.unlock();

			// copy the post-back target out: the work closure may overwrite all of scratch
			auto *origin = t->scratch_as<record>().origin;
			t->complete();
			__event_loop::post(*origin, task_ptr{t});

			lock.lock();
		}
		else if (stop)
		{
			return;
		}
		else
		{
			pending.wait(lock);
		}
	}
}

void submit (impl_type &pool, task &t) noexcept
{
	{
		const std::scoped_lock lock{pool.mutex};
		pool.queue.push(t);
	}
	pool.pending.notify_one();
}

void deleter::operator() (impl_type *pool) const noexcept
{
	delete pool;
}

} // namespace __thread_pool

result<thread_pool> make_thread_pool (size_t threads) noexcept
{
	using namespace __thread_pool;

	pal_require(threads > 0, "thread_pool with no worker threads");

	auto *self = new (std::nothrow) impl_type{};
	if (self == nullptr)
	{
		return make_unexpected(std::errc::not_enough_memory);
	}

	impl_ptr impl{self};
	try
	{
		impl->workers.reserve(threads);
		while (impl->workers.size() < threads)
		{
			impl->workers.emplace_back(&impl_type::run, self);
		}
	}
	catch (const std::system_error &e)
	{
		return unexpected{e.code()};
	}
	catch (...)
	{
		return make_unexpected(std::errc::not_enough_memory);
	}

	return thread_pool{std::move(impl)};
}

} // namespace pal::async
