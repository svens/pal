// udp_relay_client is client side for udp_relay_server_XXX
// - it connects to relay port 3478 and registers itself with 64-bit integer
// - then it starts sending periodically packets to relay port 3479 and
//   expects data to be sent back to same port from where initial registration
//   was sent

// Note: this client does not tolerate cumulative packet loss over 10sec:
// receive requests remain in pending state, not resending packets. It is easy
// to fix (close/open socket, move pending requests back to available queue,
// re-register with new id to relay) but for our purposes we don't care.

#include <samples/command_line>
#include <samples/metrics>

#include <pal/net/async/request>
#include <pal/net/async/service>
#include <pal/net/internet>

#include <chrono>
#include <deque>
#include <iostream>
#include <random>


using namespace std::chrono_literals;

using protocol = pal::net::ip::udp;
using endpoint_type = protocol::endpoint;
using socket_type = protocol::socket;


struct config //{{{1
{
	//
	// compile time config
	//

	static constexpr size_t max_payload_size = 1'200;

	//
	// runtime config
	//

	size_t kbps = 59;
	size_t payload = 200;
	size_t streams = 1;

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
				payload = std::clamp(
					parse<size_t>(option, argument),
					sizeof(uint64_t),
					max_payload_size
				);
			}
			else if (option == "streams")
			{
				streams = parse<size_t>(option, argument);
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
	}

	void print () const
	{
		std::cout << "config:"
			<< "\n > kbps=" << kbps
			<< "\n > payload=" << payload
			<< "\n > streams=" << streams
			<< "\n > server=" << server
			<< "\n > client.port=" << client.port
			<< "\n > peer.port=" << peer.port
			<< '\n';
	}
};


class stream //{{{1
{
public:

	stream ()
	{
		while (pool_.size() != max_pending_requests)
		{
			available_.push(&pool_.emplace_back(*this).handle);
		}
		register_stream();
	}

	void tick ();

	static int run_all (const config &config);

private:

	//
	// per stream data
	//

	uint64_t id_{};
	socket_type socket_{};
	pal::net::async::service::clock_type::time_point next_tick_{};

	void register_stream ()
	{
		id_ = ++id;
		for (auto &request: pool_)
		{
			*reinterpret_cast<uint64_t *>(request.data) = id_;
		}

		next_tick_ = service.now() + std::chrono::seconds{id_ % 10};
		socket_ = pal_try(pal::net::make_datagram_socket(protocol::v4()));
		pal_try(service.make_async(socket_));
		pal_try(socket_.send_to(std::span{&id_, 1}, client_endpoint));
	}

	struct request
	{
		pal::net::async::request handle{};
		char data[config::max_payload_size] = {};
		void(request::*on_completion)() = {};
		stream &owner;

		request (stream &owner)
			: owner{owner}
		{ }

		void on_peer_send ()
		{
			auto &send_to = std::get<pal::net::async::send_to>(handle);
			metrics.inc(metrics.out, send_to.bytes_transferred);
			on_completion = &request::on_client_receive;
			owner.socket_.async_receive(&handle, std::span{data});
		}

		void on_client_receive ()
		{
			auto &receive = std::get<pal::net::async::receive>(handle);
			metrics.inc(metrics.in, receive.bytes_transferred);
			owner.available_.push(&handle);
		}

		static void ready (pal::net::async::request *request)
		{
			auto &that = *reinterpret_cast<stream::request *>(request);
			std::invoke(that.on_completion, that);
		}
	};

	std::deque<request> pool_{};
	pal::net::async::request_queue available_{};


	//
	// common streams' data
	//

	static uint64_t init_id ()
	{
		static std::random_device dev;
		static std::default_random_engine eng(dev());
		static std::uniform_int_distribution<uint64_t> dist;
		return dist(eng);
	}

	// initialized by stream::run_all()
	static inline uint64_t id = init_id();
	static inline pal::net::async::service service = pal_try(pal::net::async::make_service());
	static inline endpoint_type client_endpoint, peer_endpoint;
	static inline std::chrono::milliseconds tick_interval;
	static inline size_t max_pending_requests;
	static inline size_t payload_size;

	static inline ::metrics metrics{};
	static void print_metrics ();
};


void stream::tick ()
{
	if (next_tick_ > service.now())
	{
		return;
	}
	next_tick_ = service.now() + tick_interval;

	if (auto *request = reinterpret_cast<stream::request *>(available_.try_pop()))
	{
		request->on_completion = &request::on_peer_send;
		socket_.async_send_to(
			&request->handle,
			std::span{request->data, payload_size},
			peer_endpoint
		);
	}
}


int stream::run_all (const config &config)
{
	config.print();

	client_endpoint = {config.server, config.client.port};
	peer_endpoint = {config.server, config.peer.port};

	const auto stream_bytes_per_sec = config.kbps * 1000 / 8;
	const auto total_bytes_per_sec = stream_bytes_per_sec * config.streams;
	const auto total_packets_per_min = 60 * total_bytes_per_sec / config.payload;
	const auto stream_packets_per_min = total_packets_per_min / config.streams;

	tick_interval = 60000ms / stream_packets_per_min;
	max_pending_requests = 10 * stream_packets_per_min / 60;
	payload_size = config.payload;

	std::cout << "connecting: " << config.streams << " stream(s)\n";
	std::deque<stream> streams;
	while (streams.size() != config.streams)
	{
		streams.emplace_back();
	}

	const auto [bps, unit] = metrics::bits_per_sec(total_bytes_per_sec, 1s);
	std::cout << "streaming: " << bps << unit << ", " << total_packets_per_min/60 << "pps\n";
	while (true)
	{
		service.run_for(tick_interval / 2, &stream::request::ready);
		for (auto &stream: streams)
		{
			stream.tick();
		}
		print_metrics();
	}

	return EXIT_SUCCESS;
}


void stream::print_metrics ()
{
	static const auto interval = 5s;
	static auto next_print_ = service.now() + interval;

	if (next_print_ > service.now())
	{
		return;
	}
	next_print_ = service.now() + interval;

	::metrics total;
	metrics.load_and_reset(total);
	auto [in_bps, in_unit] = metrics::bits_per_sec(total.in.bytes, interval);
	auto [out_bps, out_unit] = metrics::bits_per_sec(total.out.bytes, interval);
	total.in.packets /= interval.count();
	total.out.packets /= interval.count();

	std::cout
		<< " > in: " << total.in.packets << '/' << in_bps << in_unit
		<< " | out: " << total.out.packets << '/' << out_bps << out_unit
		<< '\n';
}

//}}}1


int main (int argc, const char *argv[])
{
	try
	{
		return stream::run_all(config{argc, argv});
	}
	catch (const std::exception &e)
	{
		std::cerr << argv[0] << ": " << e.what() << '\n';
		return EXIT_FAILURE;
	}
}
