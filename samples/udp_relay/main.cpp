#include <samples/common/command_line.hpp>
#include <samples/common/metric.hpp>
#include <samples/common/relay.hpp>
#include <samples/common/thread.hpp>

#include <pal/async/datagram_socket.hpp>
#include <pal/async/event_loop.hpp>
#include <pal/async/task_pool.hpp>
#include <pal/net/ip/udp.hpp>
#include <pal/net/socket_option.hpp>
#include <pal/version.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <deque>
#include <format>
#include <mutex>
#include <print>
#include <ranges>
#include <span>
#include <system_error>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <utility>

#if __pal_os_linux
	#include <linux/filter.h>
#endif

using namespace std::chrono;
using namespace std::chrono_literals;

using protocol = pal::net::ip::udp;
using endpoint_type = protocol::endpoint;
using sync_socket = protocol::socket;
using async_socket = pal::async::handle<sync_socket>;
using receive_result = pal::result<async_socket::receive_from_result>;
using send_result = pal::result<async_socket::send_to_result>;

// NOLINTBEGIN(bugprone-exception-escape): OOM, terminating is fine

namespace
{

std::atomic<bool> stopped = false;

void on_signal (int)
{
	stopped = true;
}

enum class relay_mode
{
	mturn,
};

constexpr std::string_view to_string_view (relay_mode mode) noexcept
{
	switch (mode)
	{
		case relay_mode::mturn: return "mturn";
	}
	return {};
}

relay_mode parse_mode (std::string_view option, std::string_view argument)
{
	if (argument == to_string_view(relay_mode::mturn))
	{
		return relay_mode::mturn;
	}
	throw std::runtime_error(std::format("{}: invalid mode '{}'", option, argument));
}

struct config
{
	static constexpr auto stop_check_interval = 1s;
	static constexpr size_t client_task_count = 256;
	static constexpr size_t client_buffer_size = 16;
	static constexpr size_t peer_task_count = 2'000;

	static inline const pal::net::send_buffer_size send_buffer_size{2 * 1024 * 1024};
	static inline const pal::net::receive_buffer_size receive_buffer_size{2 * 1024 * 1024};

	relay_mode mode = relay_mode::mturn;

	size_t threads = std::thread::hardware_concurrency();

	pal::async::event_loop_config loop{};

	nanoseconds handler_spin{};

	struct
	{
		pal::net::ip::port_type port = pal::net::ip::port_type::stun;
	} client{};

	struct
	{
		pal::net::ip::port_type port = pal::net::ip::port_type::stun + 1;
	} peer{};

	explicit config (std::span<const char *const> args)
	{
		parse_command_line(args, [this](std::string_view option, std::string_view argument)
		{
			if (option == "mode")
			{
				mode = parse_mode(option, argument);
			}
			else if (option == "client.port")
			{
				client.port = parse<pal::net::ip::port_type>(option, argument);
			}
			else if (option == "peer.port")
			{
				peer.port = parse<pal::net::ip::port_type>(option, argument);
			}
			else if (option == "threads")
			{
				threads = parse<size_t>(option, argument);
			}
			else if (option == "drain_batch")
			{
				loop.drain_batch = parse<size_t>(option, argument);
			}
			else if (option == "poll_batch")
			{
				loop.poll_batch = parse<size_t>(option, argument);
			}
			else if (option == "handler_spin_ns")
			{
				handler_spin = nanoseconds{parse<nanoseconds::rep>(option, argument)};
			}
			else
			{
				throw std::runtime_error(std::format("invalid option '{}'", option));
			}
		});

		if (threads < 1)
		{
			throw std::runtime_error("threads: must be at least 1");
		}
	}

	void print () const
	{
		std::println("config:");
		std::println(" > mode={}", to_string_view(mode));
		std::println(" > client.port={}", client.port);
		std::println(" > peer.port={}", peer.port);
		std::println(" > threads={}", threads);
		std::println(" > poll_batch={}", loop.poll_batch);
		std::println(" > drain_batch={}", loop.drain_batch);
		std::println(" > handler_spin_ns={}", handler_spin.count());
	}
};

void spin (nanoseconds duration) noexcept
{
	if (duration <= nanoseconds::zero())
	{
		return;
	}
	const auto deadline = steady_clock::now() + duration;
	while (steady_clock::now() < deadline)
	{
	}
}

class client_map
{
public:

	using key = uint64_t;
	using value = endpoint_type;

	static void store (key id, const value &endpoint)
	{
		const std::lock_guard lock{global_index_mutex_};
		global_index_.insert_or_assign(id, endpoint);
	}

	const value *load (key id)
	{
		if (auto it = local_index_.find(id); it != local_index_.end())
		{
			return &it->second;
		}

		if (const auto *value = load_from_global(id))
		{
			local_index_.emplace(id, *value);
			return value;
		}

		return nullptr;
	}

private:

	using index = std::unordered_map<key, value>;

	index local_index_{};
	static inline index global_index_{};
	static inline std::mutex global_index_mutex_{};

	static const value *load_from_global (key id)
	{
		const std::lock_guard lock{global_index_mutex_};
		if (auto it = global_index_.find(id); it != global_index_.end())
		{
			return &it->second;
		}
		return nullptr;
	}
};

class listener
{
public:

	explicit listener (const ::config &config)
		: config_{config}
		, client_socket_{make_socket(config.client.port, thread_id_)}
		, peer_socket_{make_socket(config.peer.port, thread_id_)}
	{
		attach_load_balancer(client_socket_.native_socket(), config.threads);
		attach_load_balancer(peer_socket_.native_socket(), config.threads);
	}

	~listener ()
	{
		if (thread_.joinable())
		{
			thread_.join();
		}
	}

	void start ()
	{
		thread_ = std::thread([this]
		{
			try
			{
				this_thread::pin_to_cpu(thread_id_);
				run();
			}
			catch (const std::exception &e)
			{
				std::println(stderr, "listener: {}", e.what());
				stopped = true;
			}
		});
	}

	static traffic_metric gather_metrics (std::deque<listener> &listeners, std::span<char> ingress_distribution);

private:

	const ::config &config_;

	static inline size_t thread_id = 0;
	const size_t thread_id_{thread_id++};
	std::thread thread_{};

	sync_socket client_socket_, peer_socket_;
	pal::async::task_pool<config::client_task_count, config::client_buffer_size> client_tasks_{};
	pal::async::task_pool<config::peer_task_count, relay::max_datagram_size> peer_tasks_{};

	traffic_metric metrics_{};
	client_map clients_{};

	void run ()
	{
		auto loop = make_event_loop(config_.loop);
		{
			auto client = loop.make_handle(std::move(client_socket_)).value();
			auto peer = loop.make_handle(std::move(peer_socket_)).value();

			while (auto t = peer_tasks_.try_acquire())
			{
				start_peer_receive(peer, client, std::move(t));
			}
			while (auto t = client_tasks_.try_acquire())
			{
				start_client_receive(client, std::move(t));
			}

			while (!stopped)
			{
				std::ignore = loop.run_for(config::stop_check_interval).value();
			}
		}

		while (loop.stats().io_in_flight != 0)
		{
			std::ignore = loop.run_once().value();
		}
	}

	auto on_allocation_sent (async_socket &client) noexcept
	{
		return [this, &client](pal::async::task_ptr &&p, send_result &&r) noexcept
		{
			if (r)
			{
				metrics_.out.inc(p->span().size());
				start_client_receive(client, std::move(p));
			}
			else if (r.error() != std::errc::operation_canceled)
			{
				start_client_receive(client, std::move(p));
			}
		};
	}

	auto on_client_receive (async_socket &client) noexcept
	{
		return [this, &client](pal::async::task_ptr &&p, receive_result &&r) noexcept
		{
			if (!r)
			{
				if (r.error() != std::errc::operation_canceled)
				{
					start_client_receive(client, std::move(p));
				}
				return;
			}

			spin(config_.handler_spin);
			metrics_.in.inc(p->span().size());

			if (p->span().size() == sizeof(relay::registration))
			{
				clients_.store(relay::session(p->span()), r->endpoint);

				const relay::allocation response{
					.session = relay::session(p->span()),
					.port = static_cast<uint16_t>(config_.peer.port),
				};
				client_tasks_.reset_span(*p);
				std::memcpy(p->span().data(), &response, sizeof(response));
				p->span(p->span().first(sizeof(response)));

				client.start_reply(std::move(p), on_allocation_sent(client));
				return;
			}

			start_client_receive(client, std::move(p));
		};
	}

	void start_client_receive (async_socket &client, pal::async::task_ptr &&t) noexcept
	{
		client_tasks_.reset_span(*t);
		client.start_receive_from(std::move(t), on_client_receive(client));
	}

	auto on_forward_sent (async_socket &peer, async_socket &client) noexcept
	{
		return [this, &peer, &client](pal::async::task_ptr &&p, send_result &&r) noexcept
		{
			if (r)
			{
				metrics_.out.inc(p->span().size());
				start_peer_receive(peer, client, std::move(p));
			}
			else if (r.error() != std::errc::operation_canceled)
			{
				start_peer_receive(peer, client, std::move(p));
			}
		};
	}

	auto on_peer_receive (async_socket &peer, async_socket &client) noexcept
	{
		return [this, &peer, &client](pal::async::task_ptr &&p, receive_result &&r) noexcept
		{
			if (!r)
			{
				if (r.error() != std::errc::operation_canceled)
				{
					start_peer_receive(peer, client, std::move(p));
				}
				return;
			}

			spin(config_.handler_spin);
			metrics_.in.inc(p->span().size());

			if (p->span().size() >= sizeof(relay::header))
			{
				if (const auto *endpoint = clients_.load(relay::session(p->span())))
				{
					client.start_send_to(std::move(p), *endpoint, on_forward_sent(peer, client));
					return;
				}
			}

			start_peer_receive(peer, client, std::move(p));
		};
	}

	void start_peer_receive (async_socket &peer, async_socket &client, pal::async::task_ptr &&t) noexcept
	{
		peer_tasks_.reset_span(*t);
		peer.start_receive_from(std::move(t), on_peer_receive(peer, client));
	}

	static pal::async::event_loop make_event_loop (const pal::async::event_loop_config &config)
	{
		auto loop = pal::async::make_loop(config);
		if (loop)
		{
			return std::move(*loop);
		}
		throw std::runtime_error(std::format("event_loop: {}", loop.error().message()));
	}

	static sync_socket make_socket (pal::net::ip::port_type port, size_t thread_index)
	{
		auto socket = pal::net::make_datagram_socket(protocol::v4).value();
		socket.set_option(config::send_buffer_size).value();
		socket.set_option(config::receive_buffer_size).value();

		auto r = socket.set_option(pal::net::receive_cpu{static_cast<uint16_t>(thread_index)});
		if (!r && thread_index > 0)
		{
			throw std::system_error(r.error(), "receive_cpu");
		}

		socket.bind({pal::net::ip::address_v4::any, port}).value();
		return socket;
	}


	static void attach_load_balancer (const pal::net::native_socket &socket, size_t thread_count)
	{
		#if __pal_os_linux
		{
			sock_filter code[] =
			{
				// A = cpu;
				BPF_STMT(BPF_LD | BPF_W | BPF_ABS, uint32_t(SKF_AD_OFF + SKF_AD_CPU)),
				// A %= thread_count;
				BPF_STMT(BPF_ALU | BPF_MOD, uint32_t(thread_count)),
				// return A;
				BPF_STMT(BPF_RET | BPF_A, 0),
			};
			const sock_fprog prog =
			{
				.len = sizeof(code) / sizeof(code[0]),
				.filter = code,
			};
			socket.set_option(SOL_SOCKET, SO_ATTACH_REUSEPORT_CBPF, &prog, sizeof(prog)).value();
		}
		#else
		{
			std::ignore = socket;
			std::ignore = thread_count;
		}
		#endif
	}
};

traffic_metric listener::gather_metrics (std::deque<listener> &listeners, std::span<char> ingress_distribution)
{
	traffic_metric total{};

	std::deque<traffic_metric> listener_metrics{listeners.size()};
	for (auto [source, metric]: std::views::zip(listeners, listener_metrics))
	{
		source.metrics_.load(metric).reset();
		metric.sum(total);
	}

	auto remaining_size = static_cast<ptrdiff_t>(ingress_distribution.size());
	auto *ptr = ingress_distribution.data();
	const auto total_bytes = static_cast<double>(total.in.bytes);
	for (auto &metric: listener_metrics)
	{
		const auto share = total_bytes != 0 ? static_cast<double>(metric.in.bytes) * 100.0 / total_bytes : 0.0;
		const auto [out, size] = std::format_to_n(ptr, remaining_size, "{:.0f}%|", share);
		if (size >= remaining_size)
		{
			break;
		}
		remaining_size -= size;
		ptr = out;
	}
	if (ptr != ingress_distribution.data())
	{
		*--ptr = '\0';
	}

	return total;
}

void print_metrics (std::deque<listener> &listeners, const seconds &interval)
{
	std::array<char, 1'024> ingress_distribution{};
	const auto total = listener::gather_metrics(listeners, ingress_distribution);
	std::println(
		" > in: {} / {} | out: {} / {} | dist: {}",
		bit_rate{total.in.bytes, interval},
		packet_rate{total.in.packets, interval},
		bit_rate{total.out.bytes, interval},
		packet_rate{total.out.packets, interval},
		ingress_distribution.data()
	);
}

void run (const ::config &config)
{
	config.print();

	std::deque<listener> listeners;
	while (listeners.size() != config.threads)
	{
		listeners.emplace_back(config);
	}

	std::signal(SIGINT, on_signal);
	std::signal(SIGTERM, on_signal);

	for (auto &listener: listeners)
	{
		listener.start();
	}

	std::println("\nrunning");
	while (!stopped)
	{
		std::this_thread::sleep_for(config.stop_check_interval);
		print_metrics(listeners, config.stop_check_interval);
	}

	std::println("\nstopped");
}

} // namespace

int main (int argc, const char *argv[])
{
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	try
	{
		run(config{std::span{argv, static_cast<size_t>(argc)}});
		return EXIT_SUCCESS;
	}
	catch (const std::exception &e)
	{
		std::println(stderr, "{}: {}", argv[0], e.what());
		return EXIT_FAILURE;
	}
}

// NOLINTEND(bugprone-exception-escape)
