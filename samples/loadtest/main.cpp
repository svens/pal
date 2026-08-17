#include <samples/common/command_line.hpp>
#include <samples/common/metric.hpp>
#include <samples/common/packet_tracker.hpp>
#include <samples/common/relay.hpp>
#include <samples/common/statistics.hpp>

#include <pal/net/ip/udp.hpp>
#include <pal/net/socket_option.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <deque>
#include <format>
#include <print>
#include <random>
#include <span>
#include <system_error>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <vector>

using namespace std::chrono;
using namespace std::chrono_literals;

using protocol = pal::net::ip::udp;
using endpoint_type = protocol::endpoint;
using sync_socket = protocol::socket;

namespace
{

std::atomic<bool> stopped = false;

void on_signal (int)
{
	stopped = true;
}

struct config
{
	static constexpr auto burst_interval = 1ms;
	static constexpr auto report_interval = 2s;
	static constexpr auto handshake_timeout = 1s;
	static constexpr auto handshake_retry_delay = 100ms;
	static constexpr auto receive_poll_timeout = 100ms;
	static constexpr size_t handshake_attempts = 3;
	static constexpr size_t tracker_window = 64;

	static inline const pal::net::receive_buffer_size receive_buffer_size{4 * 1024 * 1024};

	pal::net::ip::address server = pal::net::ip::address_v4::loopback;
	pal::net::ip::port_type port = pal::net::ip::port_type::stun;
	size_t streams = 1;
	size_t kbps = 48;
	size_t payload = 120;
	seconds warmup{0};
	seconds runtime{0};
	statistics::thresholds limits{};

	explicit config (std::span<const char *const> args)
	{
		parse_command_line(args, [this](std::string_view option, std::string_view argument)
		{
			if (option == "server")
			{
				server = pal::net::ip::make_address(argument).value();
			}
			else if (option == "port")
			{
				port = parse<pal::net::ip::port_type>(option, argument);
			}
			else if (option == "streams")
			{
				streams = parse<size_t>(option, argument);
			}
			else if (option == "kbps")
			{
				kbps = parse<size_t>(option, argument);
			}
			else if (option == "payload")
			{
				payload = parse<size_t>(option, argument);
			}
			else if (option == "warmup")
			{
				warmup = parse<seconds::rep>(option, argument) * 1s;
			}
			else if (option == "runtime")
			{
				runtime = parse<seconds::rep>(option, argument) * 1s;
			}
			else if (option == "max-loss")
			{
				limits.max_loss_pct = parse<uint64_t>(option, argument);
			}
			else if (option == "max-rtt-ms")
			{
				limits.max_rtt = parse<milliseconds::rep>(option, argument) * 1ms;
			}
			else
			{
				throw std::runtime_error(std::format("invalid option '{}'", option));
			}
		});

		payload = std::clamp(payload, sizeof(relay::header), relay::max_datagram_size);

		if (streams < 1 || kbps < 1)
		{
			throw std::runtime_error("streams and kbps must be at least 1");
		}
	}

	[[nodiscard]] protocol proto () const noexcept
	{
		return server.is_v6() ? protocol::v6 : protocol::v4;
	}

	void print () const
	{
		std::println("config:");
		std::println(" > server={}:{}", server, port);
		std::println(" > streams={}", streams);
		std::println(" > kbps={}", kbps);
		std::println(" > payload={}", payload);
		std::println(" > warmup={}", warmup);
		std::println(" > runtime={}", runtime);
	}
};

struct stream
{
	uint64_t id;
	sync_socket socket;
	uint32_t seq = 0;
	packet_tracker<config::tracker_window> tracker{};

	uint64_t send (steady_clock::time_point now, std::span<std::byte> payload)
	{
		const relay::header header{.session = id, .seq = seq};
		std::memcpy(payload.data(), &header, sizeof(header));
		const auto lost = tracker.sent(seq, now);
		++seq;
		std::ignore = socket.send(payload);
		return lost;
	}
};

class receiver
{
public:

	receiver (const ::config &config, statistics::receive_stats &stats)
		: config_{config}
		, stats_{stats}
		, socket_{pal::net::make_datagram_socket(config.proto()).value()}
	{
		socket_.set_option(config::receive_buffer_size).value();
		socket_.connect({config.server, config.port}).value();
	}

	~receiver ()
	{
		if (thread_.joinable())
		{
			thread_.join();
		}
	}

	void setup (std::deque<stream> &streams, std::mt19937_64 &rng)
	{
		socket_.set_option(pal::net::receive_timeout{config::handshake_timeout}).value();

		for (auto count = config_.streams; count > 0; --count)
		{
			const auto id = rng();
			auto &s = streams.emplace_back(id, allocate(id));
			index_.emplace(s.id, &s);
		}

		socket_.set_option(pal::net::receive_timeout{config::receive_poll_timeout}).value();
	}

	void start ()
	{
		thread_ = std::thread([this]
		{
			try
			{
				run();
			}
			catch (const std::exception &e)
			{
				std::println(stderr, "receiver: {}", e.what());
				stopped = true;
			}
		});
	}

private:

	const ::config &config_;
	statistics::receive_stats &stats_;
	sync_socket socket_;
	std::unordered_map<uint64_t, stream *> index_{};
	std::thread thread_{};

	sync_socket allocate (uint64_t id)
	{
		const relay::registration registration{.session = id};

		for (auto attempt = config::handshake_attempts; attempt > 0; --attempt)
		{
			if (!socket_.send(as_span(registration)))
			{
				std::this_thread::sleep_for(config::handshake_retry_delay);
				continue;
			}

			while (true)
			{
				std::array<std::byte, relay::max_datagram_size> buffer{};
				const auto received = socket_.receive(std::span{buffer});
				if (!received)
				{
					if (recoverable(received.error()))
					{
						break;
					}
					throw std::system_error(received.error());
				}

				if (*received != sizeof(relay::allocation))
				{
					continue;
				}
				const auto allocation = relay::read<relay::allocation>(buffer);
				if (allocation.session != id)
				{
					continue;
				}

				auto socket = pal::net::make_datagram_socket(config_.proto()).value();
				socket.connect({config_.server, pal::net::ip::port_type{allocation.port}}).value();
				return socket;
			}
		}

		throw std::runtime_error("no allocation response");
	}

	void run ()
	{
		std::array<std::byte, relay::max_datagram_size> buffer{};

		while (!stopped)
		{
			const auto received = socket_.receive(std::span{buffer});
			if (!received)
			{
				if (recoverable(received.error()))
				{
					continue;
				}
				throw std::system_error(received.error());
			}

			if (*received < sizeof(relay::header))
			{
				continue;
			}

			const auto header = relay::read<relay::header>(buffer);

			const auto it = index_.find(header.session);
			if (it == index_.end())
			{
				continue;
			}

			stats_.on_receive(*received, it->second->tracker.received(header.seq, steady_clock::now()));
		}
	}

	static bool recoverable (const std::error_code &error) noexcept
	{
		return error == std::errc::resource_unavailable_try_again
			|| error == std::errc::operation_would_block
			|| error == std::errc::timed_out
			|| error == std::errc::interrupted
		;
	}
};

void run_load (const ::config &config, std::deque<stream> &streams, statistics &stats)
{
	std::vector<std::byte> payload(config.payload, std::byte{});

	const uint64_t bytes_per_sec = config.streams * config.kbps * 1'000 / 8;
	std::println("\nstreaming {} / {}",
		bit_rate{bytes_per_sec, 1s},
		packet_rate{bytes_per_sec / config.payload, 1s}
	);

	const auto start = steady_clock::now();
	auto now = start;
	const auto measure_from = start + config.warmup;
	const auto stop_time = config.runtime != 0s ? measure_from + config.runtime : (steady_clock::time_point::max)();
	auto next_report = start + config::report_interval;

	if (config.warmup != 0s)
	{
		std::println("\nwarm-up ({}), thresholds not applied", config.warmup);
	}

	uint64_t bytes_sent = 0;
	size_t stream_idx = 0;
	bool measuring = false;

	while (!stopped && now < stop_time)
	{
		now = steady_clock::now();

		const auto elapsed_ms = static_cast<uint64_t>(duration_cast<milliseconds>(now - start).count());
		const auto bytes_allowance = elapsed_ms * bytes_per_sec / 1'000;

		while (bytes_allowance - bytes_sent >= payload.size())
		{
			auto &s = streams[stream_idx++ % streams.size()];
			stats.on_send(payload.size(), s.send(now, payload));
			bytes_sent += payload.size();
		}

		if (now >= next_report)
		{
			if (!measuring && next_report - config::report_interval >= measure_from)
			{
				measuring = true;
				if (config.runtime != 0s)
				{
					std::println("\nrunning ({})", config.runtime);
				}
				else
				{
					std::println("\nrunning");
				}
			}

			stats.report_and_reset(now, measuring ? config.limits : statistics::thresholds{});
			next_report += config::report_interval;
		}

		std::this_thread::sleep_for(config::burst_interval);
	}

	stopped = true;
}

int run (const ::config &config)
{
	config.print();

	statistics stats{};
	receiver rx{config, stats.receive()};

	std::mt19937_64 rng{std::random_device{}()};
	std::deque<stream> streams;
	std::println("\nallocating {} streams", config.streams);
	rx.setup(streams, rng);
	std::println(" > {} streams allocated", streams.size());

	std::signal(SIGINT, on_signal);
	std::signal(SIGTERM, on_signal);

	rx.start();
	run_load(config, streams, stats);

	return stats.failed() ? EXIT_FAILURE : EXIT_SUCCESS;
}

} // namespace

// NOLINTNEXTLINE(bugprone-exception-escape): OOM, terminating is fine
int main (int argc, const char *argv[])
{
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	try
	{
		return run(config{std::span{argv, static_cast<size_t>(argc)}});
	}
	catch (const std::exception &e)
	{
		std::println(stderr, "{}: {}", argv[0], e.what());
		return EXIT_FAILURE;
	}
}
