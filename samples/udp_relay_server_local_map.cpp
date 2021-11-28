// udp_relay_server is simplified model of STUN/TURN relay:
// - client connects to relay port 3478 and registers itself with 64-bit integer (random)
// - peer sends data to port 3479. Data should be prefixed with same 64-bit
//   value that client session used to register itself
// - relay forwards data from peer to client endpoint

// udp_relay_server_local_map variant
// * register: client is registered in global map protected by
//   std::lock_guard<std::mutex>
// * lookup: on 1st peer receive client is extracted from global map
//   (protected by std::lock_guard<std::mutex>) and inserted into
//   thread-specific local map (unprotected)
// * lookup: on following peer receives, client is always looked up from
//   thread-specific local map (unprotected)

#include <samples/command_line>
#include <samples/metrics>
#include <samples/thread>

#include <pal/net/async/request>
#include <pal/net/async/service>
#include <pal/net/internet>

#include <chrono>
#include <deque>
#include <iostream>
#include <mutex>
#include <thread>
#include <unordered_map>

#if __pal_os_linux
	#include <linux/filter.h>
#endif


using namespace std::chrono_literals;

using protocol = pal::net::ip::udp;
using endpoint_type = protocol::endpoint;
using socket_type = protocol::socket;


struct config //{{{1
{
	//
	// compile time config
	//

	static constexpr std::chrono::seconds stop_check_interval = 1s;
	static constexpr size_t requests_per_thread_port = 2'000;
	static constexpr size_t max_receive_size = 1'200;

	//
	// socket settings
	//

	static inline const pal::net::reuse_port reuse_port{!pal::is_windows_build};
	static inline const pal::net::send_buffer_size send_buffer_size{4 * 1024 * 1024};
	static inline const pal::net::receive_buffer_size receive_buffer_size{4 * 1024 * 1024};

	//
	// runtime config
	//

	size_t threads = std::thread::hardware_concurrency();

	// client-side config
	struct
	{
		pal::net::ip::port_type port = 3478;
	} client{};

	// peer-side config
	struct
	{
		pal::net::ip::port_type port = 3479;
	} peer{};

	config (int argc, const char *argv[])
	{
		parse_command_line(argc, argv, [this](std::string option, std::string argument)
		{
			if (option == "client.port")
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
			else
			{
				throw std::runtime_error("invalid option '" + option + "'");
			}
		});
	}

	void print () const
	{
		std::cout << "config:"
			<< "\n > client.port=" << client.port
			<< "\n > peer.port=" << peer.port
			<< "\n > threads=" << threads
			<< '\n';
	}
};


class listener //{{{1
{
public:

	listener (const config &config, bool &stopped)
		: config_{config}
		, stopped_{stopped}
		, thread_id_{thread_id++}
		, service_{pal_try(pal::net::async::make_service())}
		, client_{make_socket(config_.client.port)}
		, peer_{make_socket(config_.peer.port)}
	{
		pal_try(service_.make_async(client_));
		pal_try(service_.make_async(peer_));
	}

	~listener ()
	{
		if (thread_.joinable())
		{
			thread_.join();
		}
	}

	void start ();

	template <size_t N>
	static metrics load_metrics (
		std::deque<listener> &listeners,
		char (&ingress_distribution)[N]
	);

private:

	const config &config_;
	bool &stopped_;

	static inline size_t thread_id = 0;
	const size_t thread_id_;
	std::thread thread_{};

	pal::net::async::service service_;
	socket_type client_, peer_;
	metrics metrics_{};

	using client_id = uint64_t;
	using client_map = std::unordered_map<client_id, endpoint_type>;

	client_map client_map_{};
	static inline client_map global_client_map_{};
	static inline std::mutex global_client_map_mutex_{};

	static client_id get_client_id (const void *data) noexcept
	{
		return *reinterpret_cast<const client_id *>(data);
	}

	static void register_client (client_id id, const endpoint_type &endpoint)
	{
		std::lock_guard lock{global_client_map_mutex_};
		global_client_map_.emplace(id, endpoint);
	}

	static auto try_extract_client (client_id id)
	{
		std::lock_guard lock{global_client_map_mutex_};
		return global_client_map_.extract(id);
	}

	const endpoint_type *try_load (client_id id)
	{
		if (auto it = client_map_.find(id);  it != client_map_.end())
		{
			// hot-path: found in local map
			return &it->second;
		}

		if (auto node = try_extract_client(id))
		{
			// cold-path: found in global map, move to local map
			const auto *result = &node.mapped();
			client_map_.insert(std::move(node));
			return result;
		}

		// not found
		return nullptr;
	}

	struct request
	{
		pal::net::async::request handle{};
		char data[config::max_receive_size] = {};
		endpoint_type endpoint{};

		::listener &listener;
		void(request::*on_completion)();

		struct client_tag {};
		struct peer_tag {};

		request (::listener &listener, const client_tag &)
			: listener{listener}
			, on_completion{&request::on_client_receive}
		{
			listener.client_.async_receive_from(&handle, std::span{data}, endpoint);
		}

		request (::listener &listener, const peer_tag &)
			: listener{listener}
			, on_completion{&request::on_peer_receive}
		{
			listener.peer_.async_receive(&handle, std::span{data});
		}

		static void ready (pal::net::async::request *request)
		{
			auto &that = *reinterpret_cast<listener::request *>(request);
			std::invoke(that.on_completion, that);
		}

		void on_client_receive ()
		{
			auto &receive_from = std::get<pal::net::async::receive_from>(handle);
			listener.metrics_.inc(listener.metrics_.in, receive_from.bytes_transferred);
			if (receive_from.bytes_transferred == sizeof(client_id))
			{
				register_client(get_client_id(data), endpoint);
			}
			listener.client_.async_receive_from(&handle, std::span{data}, endpoint);
		}

		void on_peer_receive ()
		{
			auto &receive = std::get<pal::net::async::receive>(handle);
			listener.metrics_.inc(listener.metrics_.in, receive.bytes_transferred);
			if (receive.bytes_transferred >= sizeof(client_id))
			{
				if (auto *client_endpoint = listener.try_load(get_client_id(data)))
				{
					on_completion = &request::on_client_send;
					listener.client_.async_send_to(
						&handle,
						std::span{data, receive.bytes_transferred},
						*client_endpoint
					);
					return;
				}
			}
			listener.peer_.async_receive(&handle, std::span{data});
		}

		void on_client_send ()
		{
			auto &send_to = std::get<pal::net::async::send_to>(handle);
			listener.metrics_.inc(listener.metrics_.out, send_to.bytes_transferred);
			on_completion = &request::on_peer_receive;
			listener.peer_.async_receive(&handle, std::span{data});
		}
	};

	void io_thread ();
	socket_type make_socket (pal::net::ip::port_type port);
	void attach_load_balancer (socket_type &socket);
};


void listener::start ()
{
	attach_load_balancer(client_);
	attach_load_balancer(peer_);

	thread_ = std::thread([this]
	{
		try
		{
			io_thread();
		}
		catch (const std::exception &e)
		{
			std::cerr << "listener: " << e.what() << '\n';
			stopped_ = true;
		}
	});
}


template <size_t N>
metrics listener::load_metrics (std::deque<listener> &listeners, char (&ingress_distribution)[N])
{
	// not thread-safe but good enough
	metrics total{};

	// gather and reset per listener metrics
	std::deque<metrics> listener_metrics{listeners.size()};
	for (auto i = 0u;  i < listeners.size();  ++i)
	{
		listeners[i].metrics_.load(listener_metrics[i]).reset();
		listener_metrics[i].sum(total);
	}

	// calculate ingress distribution over listeners
	auto remaining_size = N;
	auto *ptr = ingress_distribution;
	for (auto &metric: listener_metrics)
	{
		auto n = std::snprintf(ptr, remaining_size, "%.f%%|",
			(total.in.bytes ? (metric.in.bytes * 100.0 / total.in.bytes) : 0.0)
		);
		if (n < 0 || (remaining_size -= n) < 0)
		{
			break;
		}
		ptr += n;
	}
	if (ptr != ingress_distribution)
	{
		*--ptr = '\0';
	}

	return total;
}


void listener::io_thread ()
{
	this_thread::pin_to_cpu(thread_id_);

	std::deque<request> client_requests, peer_requests;
	for (auto i = 0u;  i < config_.requests_per_thread_port;  ++i)
	{
		client_requests.emplace_back(*this, request::client_tag{});
		peer_requests.emplace_back(*this, request::peer_tag{});
	}

	while (!stopped_)
	{
		service_.run_for(config_.stop_check_interval, &request::ready);
	}
}


socket_type listener::make_socket (pal::net::ip::port_type port)
{
	auto socket = pal_try(pal::net::make_datagram_socket(protocol::v4()));

	if (config_.send_buffer_size.value() > 0)
	{
		pal_try(socket.set_option(config_.send_buffer_size));
	}

	if (config_.receive_buffer_size.value() > 0)
	{
		pal_try(socket.set_option(config_.receive_buffer_size));
	}

	if (config_.reuse_port)
	{
		pal_try(socket.set_option(config_.reuse_port));
	}

	#if __pal_os_windows
	{
		#if !defined(SIO_SET_PORT_SHARING_PER_PROC_SOCKET)
			constexpr auto SIO_SET_PORT_SHARING_PER_PROC_SOCKET = _WSAIOW(IOC_VENDOR, 21);
		#endif

		DWORD bytes{};
		auto id = static_cast<uint16_t>(thread_id_);
		auto rv = ::WSAIoctl(
			socket.native_handle(),
			SIO_SET_PORT_SHARING_PER_PROC_SOCKET,
			&id,
			sizeof(id),
			nullptr,
			0,
			&bytes,
			nullptr,
			nullptr
		);

		if (rv != NO_ERROR && id > 0)
		{
			throw std::system_error(
				::WSAGetLastError(),
				std::system_category(),
				"WSAIoctl(SIO_SET_PORT_SHARING_PER_PROC_SOCKET)"
			);
		}
	}
	#endif

	pal_try(socket.bind({pal::net::ip::address_v4::any(), port}));
	return socket;
}


void listener::attach_load_balancer (socket_type &socket)
{
	#if __pal_os_linux
	{
		static sock_filter code[] =
		{
			// A = cpu;
			BPF_STMT(BPF_LD | BPF_W | BPF_ABS, uint32_t(SKF_AD_OFF + SKF_AD_CPU)),
			// A %= workers;
			BPF_STMT(BPF_ALU | BPF_MOD, uint32_t(config_.threads)),
			// return A;
			BPF_STMT(BPF_RET | BPF_A, 0),
		};
		static sock_fprog prog =
		{
			.len = 3,
			.filter = code,
		};
		auto rv = ::setsockopt(
			socket.native_handle(),
			SOL_SOCKET,
			SO_ATTACH_REUSEPORT_CBPF,
			&prog,
			sizeof(prog)
		);
		if (rv == -1)
		{
			throw std::system_error(errno, std::generic_category(), "attach_load_balancer");
		}
	}
	#else
	{
		(void)socket;
	}
	#endif
}


//}}}1


void print_metrics (std::deque<listener> &listeners, const std::chrono::seconds &interval)
{
	char ingress_distribution[1024];
	auto metrics = listener::load_metrics(listeners, ingress_distribution);
	auto [in_bps, in_unit] = metrics::bits_per_sec(metrics.in.bytes, interval);
	auto [out_bps, out_unit] = metrics::bits_per_sec(metrics.out.bytes, interval);
	metrics.in.packets /= interval.count();
	metrics.out.packets /= interval.count();
	std::cout
		<< " > in: " << in_bps << in_unit << '/' << metrics.in.packets << "pps"
		<< " | out: " << out_bps << out_unit << '/' << metrics.out.packets << "pps"
		<< " | dist: " << ingress_distribution
		<< '\n'
	;
}


int run (const ::config &config)
{
	bool stopped = false;

	std::deque<listener> listeners;
	while (listeners.size() != config.threads)
	{
		listeners.emplace_back(config, stopped);
	}

	for (auto &listener: listeners)
	{
		listener.start();
	}

	std::cout << "running\n";
	while (!stopped)
	{
		std::this_thread::sleep_for(config.stop_check_interval);
		print_metrics(listeners, config.stop_check_interval);
	}
	std::cout << "stopped\n";

	return EXIT_SUCCESS;
}


int main (int argc, const char *argv[])
{
	try
	{
		::config config{argc, argv};
		config.print();
		return run(config);
	}
	catch (const std::exception &e)
	{
		std::cerr << argv[0] << ": " << e.what() << '\n';
		return EXIT_FAILURE;
	}
}
