// udp_relay_client is client side for udp_relay_server_XXX
// - it connects to relay port 3478 and registers itself with 64-bit integer
// - then it starts sending periodically packets to relay port 3479 and
//   expects data to be sent back to same port from where initial registration
//   was sent

#include <samples/command_line>
#include <samples/metrics>
#include <samples/thread>

#include <pal/net/async/request>
#include <pal/net/async/service>
#include <pal/net/internet>

#include <chrono>
#include <deque>
#include <iostream>
#include <random>
#include <thread>

#if __pal_os_linux || __pal_os_macos
	#include <sys/resource.h>
#endif


using namespace std::chrono_literals;

using protocol = pal::net::ip::udp;
using endpoint_type = protocol::endpoint;
using socket_type = protocol::socket;
using service_type = pal::net::async::service;


class config //{{{1
{
public:

	//
	// compile time config
	//

	static constexpr size_t max_payload_size = 1'200;

	static inline const pal::net::send_buffer_size send_buffer_size{4 * 1024 * 1024};
	static inline const pal::net::receive_buffer_size receive_buffer_size{4 * 1024 * 1024};

	//
	// user-provided startup config
	//

	size_t kbps = 59;
	size_t payload = 200;
	size_t streams = 1;
	size_t threads = 1;

	pal::net::ip::address_v4 server = pal::net::ip::address_v4::loopback();

	struct
	{
		pal::net::ip::port_type port = 3478;
	} client{};

	struct
	{
		pal::net::ip::port_type port = 3479;
	} peer{};

	config (int argc, const char *argv[])
	{
		parse_command_line(argc, argv, [this](std::string option, std::string argument)
		{
			if (option == "kbps")
			{
				kbps = parse<size_t>(option, argument);
			}
			else if (option == "payload")
			{
				payload = std::clamp(parse<size_t>(option, argument), sizeof(uint64_t), max_payload_size);
			}
			else if (option == "streams")
			{
				streams = std::max<size_t>(parse<size_t>(option, argument), 1);
			}
			else if (option == "threads")
			{
				threads = parse<size_t>(option, argument);
			}
			else if (option == "server")
			{
				server = *pal::net::ip::make_address_v4(argument).or_else([&](auto error_code)
				{
					throw std::system_error(error_code, option + '=' + argument);
				});
			}
			else if (option == "client.port")
			{
				client.port = parse<pal::net::ip::port_type>(option, argument);
			}
			else if (option == "peer.port")
			{
				peer.port = parse<pal::net::ip::port_type>(option, argument);
			}
			else
			{
				throw std::runtime_error("invalid option '" + option + "'");
			}
		});
		init_runtime_config();
	}

	void print () const
	{
		std::cout << "config:"
			<< "\n > kbps=" << kbps
			<< "\n > payload=" << payload
			<< "\n > streams=" << streams
			<< "\n > threads=" << threads
			<< "\n > server=" << server
			<< "\n > client.port=" << client.port
			<< "\n > peer.port=" << peer.port
			<< '\n'
		;
	}

	socket_type make_socket () const
	{
		auto socket = pal_try(pal::net::make_datagram_socket(protocol::v4()));

		if (send_buffer_size.value() > 0)
		{
			pal_try(socket.set_option(send_buffer_size));
		}

		if (receive_buffer_size.value() > 0)
		{
			pal_try(socket.set_option(receive_buffer_size));
		}

		return socket;
	}

private:

	void init_runtime_config ()
	{
		const auto max_threads = std::min<size_t>(streams, std::thread::hardware_concurrency());
		threads = std::clamp<size_t>(threads, 1, max_threads);
	}
};


class stream //{{{1
{
public:

	const uint64_t id;

	stream (uint64_t id, const config &config)
		: id{id}
		, config_{config}
		, socket_{config.make_socket()}
		, blob_{data, config_.payload}
	{
		*reinterpret_cast<uint64_t *>(data) = id;
		pal_try(socket_.connect({config_.server, config_.peer.port}));
	}

	size_t send ()
	{
		return pal_try(socket_.send(blob_));
	}

private:

	const config &config_;
	socket_type socket_;
	char data[config::max_payload_size];
	const std::span<char> blob_;
};

using stream_list = std::deque<stream>;


class receiver //{{{1
{
public:

	static inline bool stopped = false;
	::metrics metrics{};

	receiver (const config &config, stream_list &streams, size_t begin, size_t end)
		: config_{config}
		, socket_{config.make_socket()}
		, service_{pal_try(pal::net::async::make_service())}
	{
		init(streams, begin, end);
	}

	~receiver ()
	{
		if (thread_.joinable())
		{
			thread_.join();
		}
	}

	void start ()
	{
		static int thread_id = 1;
		auto id = thread_id++;

		thread_ = std::thread([this,id]
		{
			try
			{
				this_thread::pin_to_cpu(id);
				#if __pal_os_linux
				{
					::setsockopt(socket_.native_handle(),
						SOL_SOCKET,
						SO_INCOMING_CPU,
						&id, sizeof(id)
					);
				}
				#endif
				run();
			}
			catch (const std::exception &e)
			{
				std::cerr << "receiver: " << e.what() << '\n';
				stopped = true;
			}
		});
	}

private:

	const config &config_;
	socket_type socket_;
	service_type service_;
	std::thread thread_{};

	// all receives share same data, we don't use content anyway
	char data_[config::max_payload_size] = {};
	std::span<char, sizeof(data_)> span_{data_};
	std::deque<pal::net::async::request> pool_{};

	void init (stream_list &streams, size_t begin, size_t end);
	void run ();
};

using receiver_list = std::deque<receiver>;


void receiver::init (stream_list &streams, size_t begin, size_t end)
{
	pal_try(service_.make_async(socket_));
	pal_try(socket_.connect({config_.server, config_.client.port}));

	for (auto it = begin;  it != end;  ++it)
	{
		pal_try(socket_.send(std::span{&streams[it].id, 1}));
	}

	pool_.resize((end - begin) * 10);
	for (auto &request: pool_)
	{
		socket_.async_receive(&request, span_);
	}
}


void receiver::run ()
{
	while (!stopped)
	{
		service_.run([this](auto *request)
		{
			auto &receive = std::get<pal::net::async::receive>(*request);
			metrics.inc(metrics.in, receive.bytes_transferred);
			socket_.async_receive(request, span_);
		});
	}
}


//}}}


void set_max_open_handles (size_t v)
{
	#if __pal_os_linux || __pal_os_macos
	{
		rlimit limit{};
		if (::getrlimit(RLIMIT_NOFILE, &limit) == -1)
		{
			throw std::system_error(errno, std::generic_category(), "getrlimit");
		}

		if (limit.rlim_cur >= v)
		{
			return;
		}

		limit.rlim_cur = v;
		if (::setrlimit(RLIMIT_NOFILE, &limit) == -1)
		{
			throw std::system_error(errno, std::generic_category(), "setrlimit");
		}
	}
	#else
	{
		(void)v;
	}
	#endif
}


void run (const config &config)
{
	config.print();

	const auto receiver_count = config.threads > 1 ? config.threads - 1 : 1;
	set_max_open_handles(config.streams + receiver_count + 10);
	this_thread::pin_to_cpu(0);

	std::random_device dev;
	uint64_t next_stream_id = dev();

	stream_list streams;
	while (streams.size() != config.streams)
	{
		streams.emplace_back(next_stream_id++, config);
	}

	const auto streams_per_receiver = config.streams / receiver_count;
	auto reminder = config.streams % receiver_count;
	size_t begin = 0;

	receiver_list receivers;
	while (receivers.size() != receiver_count)
	{
		auto end = begin + streams_per_receiver;
		if (reminder > 0)
		{
			end++, reminder--;
		}
		receivers.emplace_back(config, streams, begin, end).start();
		begin = end;
	}

	double bytes_per_sec = config.streams * config.kbps * 1000.0 / 8;
	const auto [bps, unit] = metrics::bits_per_sec(static_cast<size_t>(bytes_per_sec), 1s);
	std::cout << "streaming " << bps << unit << '/' << (bytes_per_sec / config.payload) << "pps\n";

	constexpr auto burst_interval = 1ms;
	constexpr auto bursts_per_sec = 1s / burst_interval;
	auto bytes_per_burst = bytes_per_sec / bursts_per_sec;

	auto now = std::chrono::steady_clock::now();
	auto start_time = now;
	size_t bytes_sent = 0, stream = 0;

	constexpr auto metrics_print_interval = 1s;
	auto next_metrics_print = now + metrics_print_interval;
	::metrics metrics{};

	while (!receiver::stopped)
	{
		auto run_time = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time);
		auto bytes_allowance = run_time.count() * bytes_per_burst;

		while (bytes_allowance - bytes_sent >= config.payload)
		{
			auto sent = streams[stream++ % streams.size()].send();
			metrics.inc(metrics.out, sent);
			bytes_sent += sent;
		}

		if (next_metrics_print < now)
		{
			for (auto &receiver: receivers)
			{
				receiver.metrics.sum(metrics).reset();
			}

			auto [out_bps, out_unit] = metrics::bits_per_sec(metrics.out.bytes, metrics_print_interval);
			auto [in_bps, in_unit] = metrics::bits_per_sec(metrics.in.bytes, metrics_print_interval);
			std::cout
				<< " > out: " << out_bps << out_unit << '/' << metrics.out.packets << "pps"
				<< " | in: " << in_bps << in_unit << '/' << metrics.in.packets << "pps"
				<< '\n'
			;

			metrics.reset();
			next_metrics_print += metrics_print_interval;
		}
		else
		{
			std::this_thread::sleep_for(burst_interval);
		}

		now = std::chrono::steady_clock::now();
	}
}


int main (int argc, const char *argv[])
{
	try
	{
		run(config{argc, argv});
		return EXIT_SUCCESS;
	}
	catch (const std::exception &e)
	{
		std::cerr << argv[0] << ": " << e.what() << '\n';
		return EXIT_FAILURE;
	}
}
