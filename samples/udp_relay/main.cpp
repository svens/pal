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
#include <optional>
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
	turn,
};

constexpr std::string_view to_string_view (relay_mode mode) noexcept
{
	switch (mode)
	{
		case relay_mode::mturn: return "mturn";
		case relay_mode::turn: return "turn";
	}
	return {};
}

relay_mode parse_mode (std::string_view option, std::string_view argument)
{
	if (argument == to_string_view(relay_mode::mturn))
	{
		return relay_mode::mturn;
	}
	if (argument == to_string_view(relay_mode::turn))
	{
		return relay_mode::turn;
	}
	throw std::runtime_error(std::format("{}: invalid mode '{}'", option, argument));
}

struct config
{
	static constexpr auto stop_check_interval = 1s;
	static constexpr size_t client_task_count = 256;
	static constexpr size_t client_buffer_size = 16;
	static constexpr size_t peer_task_count = 2'000;
	static constexpr size_t session_task_count = 2;

	static inline const pal::net::send_buffer_size send_buffer_size{2 * 1024 * 1024};
	static inline const pal::net::receive_buffer_size receive_buffer_size{2 * 1024 * 1024};
	static inline const pal::net::receive_buffer_size session_receive_buffer_size{256 * 1024};

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
		pal::net::ip::port_type first = pal::net::ip::port_type::stun + 1;
		pal::net::ip::port_type last = pal::net::ip::port_type::unspecified;
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
			else if (option == "peer.ports")
			{
				parse_port_range(option, argument);
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

		if (mode == relay_mode::mturn && peer.last != pal::net::ip::port_type::unspecified)
		{
			throw std::runtime_error("peer.ports: mode 'mturn' shares one port, not a range");
		}

		if (mode == relay_mode::turn)
		{
			if (peer.last == pal::net::ip::port_type::unspecified)
			{
				throw std::runtime_error("peer.ports: mode 'turn' needs a port range");
			}
			if (peer_port_count() < threads)
			{
				throw std::runtime_error("peer.ports: fewer ports than threads");
			}
		}
	}

	[[nodiscard]] size_t peer_port_count () const noexcept
	{
		return static_cast<size_t>(peer.last) - static_cast<size_t>(peer.first) + 1;
	}

	void print () const
	{
		std::println("config:");
		std::println(" > mode={}", to_string_view(mode));
		std::println(" > client.port={}", client.port);
		if (peer.last == pal::net::ip::port_type::unspecified)
		{
			std::println(" > peer.ports={}", peer.first);
		}
		else
		{
			std::println(" > peer.ports={}-{}", peer.first, peer.last);
		}
		std::println(" > threads={}", threads);
		std::println(" > poll_batch={}", loop.poll_batch);
		std::println(" > drain_batch={}", loop.drain_batch);
		std::println(" > handler_spin_ns={}", handler_spin.count());
	}

private:

	void parse_port_range (std::string_view option, std::string_view argument)
	{
		const auto separator = argument.find('-');
		if (separator == argument.npos)
		{
			peer.first = parse<pal::net::ip::port_type>(option, argument);
			peer.last = pal::net::ip::port_type::unspecified;
		}
		else
		{
			peer.first = parse<pal::net::ip::port_type>(option, argument.substr(0, separator));
			peer.last = parse<pal::net::ip::port_type>(option, argument.substr(separator + 1));

			if (peer.last <= peer.first)
			{
				throw std::runtime_error(std::format("{}: invalid port range '{}'", option, argument));
			}
		}

		if (peer.first == pal::net::ip::port_type::unspecified)
		{
			throw std::runtime_error(std::format("{}: invalid port range '{}'", option, argument));
		}
	}
};

endpoint_type client_endpoint (const endpoint_type &source, const relay::registration &request) noexcept
{
	return {source.address(), pal::net::ip::port_type{request.client_port}};
}

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

template <typename Self>
class basic_listener
{
public:

	basic_listener (const basic_listener &) = delete;
	basic_listener &operator= (const basic_listener &) = delete;

	void start ()
	{
		thread_ = std::thread([this]
		{
			try
			{
				this_thread::pin_to_cpu(thread_id_);
				static_cast<Self *>(this)->run();
			}
			catch (const std::exception &e)
			{
				std::println(stderr, "listener: {}", e.what());
				stopped = true;
			}
		});
	}

	void join ()
	{
		if (thread_.joinable())
		{
			thread_.join();
		}
	}

	static traffic_metric gather_metrics (std::deque<Self> &listeners, std::span<char> ingress_distribution)
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
		for (const auto &metric: listener_metrics)
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

protected:

	const ::config &config_;

	static inline size_t thread_id = 0;
	const size_t thread_id_{thread_id++};
	std::thread thread_{};

	sync_socket client_socket_;
	pal::async::task_pool<config::client_task_count, config::client_buffer_size> client_tasks_{};

	traffic_metric metrics_{};

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

private:

	friend Self;

	explicit basic_listener (const ::config &config)
		: config_{config}
		, client_socket_{make_socket(config.client.port, thread_id_)}
	{
		attach_load_balancer(client_socket_.native_socket(), config.threads);
	}
};

class mturn_listener: public basic_listener<mturn_listener>
{
public:

	explicit mturn_listener (const ::config &config)
		: basic_listener{config}
		, peer_socket_{make_socket(config.peer.first, thread_id_)}
	{
		attach_load_balancer(peer_socket_.native_socket(), config.threads);
	}

	~mturn_listener ()
	{
		join();
	}

private:

	friend class basic_listener<mturn_listener>;

	sync_socket peer_socket_;
	pal::async::task_pool<config::peer_task_count, relay::max_datagram_size> peer_tasks_{};

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

		std::ignore = loop.settle().value();
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
				const auto request = relay::read<relay::registration>(p->span());
				clients_.store(request.session, client_endpoint(r->endpoint, request));

				const relay::allocation response{
					.session = request.session,
					.port = static_cast<uint16_t>(config_.peer.first),
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
};

class turn_listener: public basic_listener<turn_listener>
{
public:

	explicit turn_listener (const ::config &config)
		: basic_listener{config}
		, next_port_{static_cast<size_t>(config.peer.first) + thread_id_ * block_size(config)}
		, last_port_{thread_id_ + 1 == config.threads
			? static_cast<size_t>(config.peer.last)
			: next_port_ + block_size(config) - 1
		}
	{
	}

	~turn_listener ()
	{
		join();
	}

private:

	friend class basic_listener<turn_listener>;

	struct session
	{
		endpoint_type client_endpoint;
		std::optional<async_socket> socket;
		pal::async::task_pool<config::session_task_count, relay::max_datagram_size> tasks{};

		session (const endpoint_type &client_endpoint, async_socket &&socket) noexcept
			: client_endpoint{client_endpoint}
			, socket{std::move(socket)}
		{
		}
	};

	size_t next_port_, last_port_;
	std::deque<session> sessions_{};

	static size_t block_size (const ::config &config) noexcept
	{
		return config.peer_port_count() / config.threads;
	}

	void run ()
	{
		auto loop = make_event_loop(config_.loop);
		{
			auto client = loop.make_handle(std::move(client_socket_)).value();

			while (auto t = client_tasks_.try_acquire())
			{
				start_client_receive(loop, client, std::move(t));
			}

			while (!stopped)
			{
				std::ignore = loop.run_for(config::stop_check_interval).value();
			}

			for (auto &session: sessions_)
			{
				session.socket.reset();
			}
		}

		std::ignore = loop.settle().value();

		sessions_.clear();
	}

	auto on_allocation_sent (pal::async::event_loop &loop, async_socket &client) noexcept
	{
		return [this, &loop, &client](pal::async::task_ptr &&p, send_result &&r) noexcept
		{
			if (r)
			{
				metrics_.out.inc(p->span().size());
				start_client_receive(loop, client, std::move(p));
			}
			else if (r.error() != std::errc::operation_canceled)
			{
				start_client_receive(loop, client, std::move(p));
			}
		};
	}

	auto on_client_receive (pal::async::event_loop &loop, async_socket &client) noexcept
	{
		return [this, &loop, &client](pal::async::task_ptr &&p, receive_result &&r) noexcept
		{
			if (!r)
			{
				if (r.error() != std::errc::operation_canceled)
				{
					start_client_receive(loop, client, std::move(p));
				}
				return;
			}

			spin(config_.handler_spin);
			metrics_.in.inc(p->span().size());

			if (p->span().size() == sizeof(relay::registration))
			{
				const auto request = relay::read<relay::registration>(p->span());
				if (const auto port = allocate(loop, client, client_endpoint(r->endpoint, request)))
				{
					const relay::allocation response{
						.session = request.session,
						.port = static_cast<uint16_t>(*port),
					};
					client_tasks_.reset_span(*p);
					std::memcpy(p->span().data(), &response, sizeof(response));
					p->span(p->span().first(sizeof(response)));

					client.start_reply(std::move(p), on_allocation_sent(loop, client));
					return;
				}
			}

			start_client_receive(loop, client, std::move(p));
		};
	}

	void start_client_receive (pal::async::event_loop &loop, async_socket &client, pal::async::task_ptr &&t) noexcept
	{
		client_tasks_.reset_span(*t);
		client.start_receive_from(std::move(t), on_client_receive(loop, client));
	}

	std::optional<pal::net::ip::port_type> allocate (
		pal::async::event_loop &loop,
		async_socket &client,
		const endpoint_type &client_endpoint) noexcept
	{
		while (next_port_ <= last_port_)
		{
			const auto port = pal::net::ip::port_type{static_cast<uint16_t>(next_port_++)};

			auto socket = make_session_socket(port);
			if (!socket)
			{
				continue;
			}

			auto handle = loop.make_handle(std::move(*socket));
			if (!handle)
			{
				return {};
			}

			auto &session = sessions_.emplace_back(client_endpoint, std::move(*handle));
			while (auto t = session.tasks.try_acquire())
			{
				start_session_receive(client, session, std::move(t));
			}

			return port;
		}

		return {};
	}

	auto on_forward_sent (async_socket &client, session &session) noexcept
	{
		return [this, &client, &session](pal::async::task_ptr &&p, send_result &&r) noexcept
		{
			if (r)
			{
				metrics_.out.inc(p->span().size());
				start_session_receive(client, session, std::move(p));
			}
			else if (r.error() != std::errc::operation_canceled)
			{
				start_session_receive(client, session, std::move(p));
			}
		};
	}

	auto on_session_receive (async_socket &client, session &session) noexcept
	{
		return [this, &client, &session](pal::async::task_ptr &&p, receive_result &&r) noexcept
		{
			if (!r)
			{
				if (r.error() != std::errc::operation_canceled)
				{
					start_session_receive(client, session, std::move(p));
				}
				return;
			}

			spin(config_.handler_spin);
			metrics_.in.inc(p->span().size());

			client.start_send_to(std::move(p), session.client_endpoint, on_forward_sent(client, session));
		};
	}

	void start_session_receive (async_socket &client, session &session, pal::async::task_ptr &&t) noexcept
	{
		if (session.socket)
		{
			session.tasks.reset_span(*t);
			session.socket->start_receive_from(std::move(t), on_session_receive(client, session));
		}
	}

	static std::optional<sync_socket> make_session_socket (pal::net::ip::port_type port) noexcept
	{
		auto socket = pal::net::make_datagram_socket(protocol::v4, {pal::net::ip::address_v4::any, port});
		if (!socket)
		{
			return {};
		}

		if (!socket->set_option(config::session_receive_buffer_size))
		{
			return {};
		}

		return std::move(*socket);
	}
};

template <typename Listener>
void print_metrics (std::deque<Listener> &listeners, const seconds &interval)
{
	std::array<char, 1'024> ingress_distribution{};
	const auto total = Listener::gather_metrics(listeners, ingress_distribution);
	std::println(
		" > in: {} / {} | out: {} / {} | dist: {}",
		bit_rate{total.in.bytes, interval},
		packet_rate{total.in.packets, interval},
		bit_rate{total.out.bytes, interval},
		packet_rate{total.out.packets, interval},
		ingress_distribution.data()
	);
}

template <typename Listener>
void run_listeners (const ::config &config)
{
	std::deque<Listener> listeners;
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

	for (auto &listener: listeners)
	{
		listener.join();
	}

	std::println("\nstopped");
}

void run (const ::config &config)
{
	config.print();

	switch (config.mode)
	{
		case relay_mode::mturn:
			run_listeners<mturn_listener>(config);
			return;

		case relay_mode::turn:
			run_listeners<turn_listener>(config);
			return;
	}
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
