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
#include <forward_list>
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

	static constexpr unsigned max_payload_size = 1'200;

	//
	// runtime config
	//

	unsigned kbps = 59;
	unsigned payload = 200;
	unsigned streams = 1;

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
				kbps = parse<unsigned>(option, argument);
			}
			else if (option == "payload")
			{
				payload = std::clamp(
					parse<unsigned>(option, argument),
					static_cast<unsigned>(sizeof(uint64_t)),
					max_payload_size
				);
			}
			else if (option == "streams")
			{
				streams = std::clamp(parse<unsigned>(option, argument), 1u, 10'000u);
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
		: id_{++id}
		, socket_{pal_try(pal::net::make_datagram_socket(protocol::v4()))}
	{
		while (pool_.size() != max_pending_requests)
		{
			available_.push(&pool_.emplace_back(*this, id_).handle);
		}
		pal_try(service.make_async(socket_));
		pal_try(socket_.send_to(std::span{&id_, 1}, client_endpoint));
	}

	static int run_all (const config &config);

private:

	//
	// per stream data
	//

	struct request
	{
		pal::net::async::request handle{};
		char data[config::max_payload_size] = {};
		void(request::*on_completion)() = {};
		stream &owner;

		request (stream &owner, uint64_t client_id) noexcept
			: owner{owner}
		{
			*reinterpret_cast<uint64_t *>(data) = client_id;
		}

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

		void on_tick ()
		{
			tick();
			service.post_after(tick_interval, &handle);
		}

		void on_metrics_print ()
		{
			metrics_print();
			service.post_after(metrics_print_interval, &handle);
		}

		static void ready (pal::net::async::request *request)
		{
			auto &that = *reinterpret_cast<stream::request *>(request);
			std::invoke(that.on_completion, that);
		}
	};

	uint64_t id_{};
	socket_type socket_{};
	std::deque<request> pool_{};
	pal::net::async::request_queue available_{};

	void start_tick ()
	{
		auto &request = pool_.emplace_back(*this, id_);
		request.on_completion = &request::on_tick;
		service.post_after(tick_interval, &request.handle);
	}

	void start_metrics_print ()
	{
		auto &request = pool_.emplace_back(*this, id_);
		request.on_completion = &request::on_metrics_print;
		service.post_after(metrics_print_interval, &request.handle);
	}

	void send ()
	{
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

	// initialized by run_all()
	static inline pal::net::async::service::clock_type::duration tick_interval;
	static inline const std::chrono::seconds metrics_print_interval = 5s;
	static inline endpoint_type client_endpoint, peer_endpoint;
	static inline size_t max_pending_requests;
	static inline size_t payload_size;

	static inline uint64_t id = init_id();
	static inline pal::net::async::service service = pal_try(pal::net::async::make_service());
	static inline std::forward_list<stream> streams;
	static inline ::metrics metrics{};

	static void tick ()
	{
		for (auto &stream: streams)
		{
			stream.send();
		}
	}

	static void metrics_print ()
	{
		::metrics total;
		metrics.load_and_reset(total);
		auto [in_bps, in_unit] = metrics::bits_per_sec(total.in.bytes, metrics_print_interval);
		auto [out_bps, out_unit] = metrics::bits_per_sec(total.out.bytes, metrics_print_interval);
		total.in.packets /= metrics_print_interval.count();
		total.out.packets /= metrics_print_interval.count();
		std::cout
			<< " > out: " << total.out.packets << '/' << out_bps << out_unit
			<< " | in: " << total.in.packets << '/' << in_bps << in_unit
			<< '\n';
	}
};


int stream::run_all (const config &config)
{
	config.print();

	client_endpoint = {config.server, config.client.port};
	peer_endpoint = {config.server, config.peer.port};

	const auto stream_bytes_per_min = 60 * config.kbps * 1000 / 8;
	const auto total_bytes_per_min = config.streams * stream_bytes_per_min;
	const auto total_packets_per_min = total_bytes_per_min / config.payload;
	const auto stream_packets_per_min = total_packets_per_min / config.streams;

	payload_size = config.payload;
	max_pending_requests = 10 * stream_packets_per_min / 60;
	tick_interval = 60000ms * config.payload / config.kbps / 8 / 1000;

	const auto [bps, unit] = metrics::bits_per_sec(total_bytes_per_min, 60s);
	std::cout
		<< "streaming: "
			<< bps << unit
			<< ", " << total_packets_per_min/60 << "pps"
			<< '\n'
		<< "connecting: "
			<< config.streams << " stream(s)"
			<< '\n'
	;

	for (auto i = 0u;  i != config.streams;  ++i)
	{
		streams.emplace_front();
	}

	streams.front().start_metrics_print();
	streams.front().start_tick();

	while (true)
	{
		service.run_for(tick_interval, &stream::request::ready);
	}

	return EXIT_SUCCESS;
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
