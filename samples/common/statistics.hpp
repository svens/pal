#pragma once

#include <samples/common/metric.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <format>
#include <optional>
#include <print>
#include <string>

using std::chrono::duration_cast;
using std::chrono::microseconds;
using std::chrono::milliseconds;
using std::chrono::steady_clock;

inline std::string format_duration (microseconds us)
{
	if (const auto ms = duration_cast<milliseconds>(us); ms.count() > 0)
	{
		return std::format("{} ms", ms.count());
	}
	return std::format("{} us", us.count());
}

class statistics
{
public:

	struct thresholds
	{
		std::optional<uint64_t> max_loss_pct{};
		std::optional<milliseconds> max_rtt{};
	};

	struct receive_stats
	{
		// Counters are individually atomic but harvested without a group snapshot: an on_receive
		// straddling the report's read sequence (~100ns) may split one packet across adjacent
		// intervals. exchange(0) conserves totals, so the error is bounded by ~1 packet per report;
		// a consistent snapshot would cost per-packet synchronization in the measurement path.
		std::atomic<uint64_t> packets{0}, bytes{0};
		std::atomic<microseconds::rep> rtt_sum_us{0}, rtt_count{0}, jitter_us{0};
		microseconds last_rtt{};

		void on_receive (size_t received_bytes, std::optional<steady_clock::duration> rtt) noexcept
		{
			packets.fetch_add(1, std::memory_order_relaxed);
			bytes.fetch_add(received_bytes, std::memory_order_relaxed);

			if (!rtt)
			{
				return;
			}

			const auto sample = duration_cast<microseconds>(*rtt);
			rtt_sum_us.fetch_add(sample.count(), std::memory_order_relaxed);
			rtt_count.fetch_add(1, std::memory_order_relaxed);

			if (last_rtt > microseconds::zero())
			{
				constexpr microseconds::rep jitter_smoothing_divisor = 16;
				auto jitter = microseconds{jitter_us.load(std::memory_order_relaxed)};
				const auto diff = sample > last_rtt ? sample - last_rtt : last_rtt - sample;
				if (diff > jitter)
				{
					jitter += (diff - jitter) / jitter_smoothing_divisor;
				}
				else
				{
					jitter -= (jitter - diff) / jitter_smoothing_divisor;
				}
				jitter_us.store(jitter.count(), std::memory_order_relaxed);
			}
			last_rtt = sample;
		}
	};

	[[nodiscard]] receive_stats &receive () noexcept
	{
		return receive_;
	}

	void on_send (size_t sent_bytes, uint64_t lost) noexcept
	{
		out_packets_.fetch_add(1, std::memory_order_relaxed);
		out_bytes_.fetch_add(sent_bytes, std::memory_order_relaxed);
		out_lost_.fetch_add(lost, std::memory_order_relaxed);
	}

	[[nodiscard]] bool failed () const noexcept
	{
		return failed_;
	}

	bool report_and_reset (steady_clock::time_point now, const thresholds &limits)
	{
		const auto elapsed = duration_cast<milliseconds>(now - interval_start_);
		if (elapsed.count() == 0)
		{
			return true;
		}

		const auto out_packets = out_packets_.exchange(0, std::memory_order_relaxed);
		const auto out_bytes = out_bytes_.exchange(0, std::memory_order_relaxed);
		const auto out_lost = out_lost_.exchange(0, std::memory_order_relaxed);

		const auto in_packets = receive_.packets.exchange(0, std::memory_order_relaxed);
		const auto in_bytes = receive_.bytes.exchange(0, std::memory_order_relaxed);
		const auto rtt_sum_us = receive_.rtt_sum_us.exchange(0, std::memory_order_relaxed);
		const auto rtt_count = receive_.rtt_count.exchange(0, std::memory_order_relaxed);
		const auto jitter_us = receive_.jitter_us.load(std::memory_order_relaxed);

		const microseconds rtt{rtt_count > 0 ? rtt_sum_us / rtt_count : 0};
		const microseconds jitter{jitter_us};
		const auto loss_pct = out_packets > 0 ? out_lost * 100 / out_packets : 0;

		std::println(
			"out: {} / {} | in: {} / {} | loss: {}% | rtt: {} | jitter: {}",
			bit_rate{out_bytes, elapsed},
			packet_rate{out_packets, elapsed},
			bit_rate{in_bytes, elapsed},
			packet_rate{in_packets, elapsed},
			loss_pct,
			format_duration(rtt),
			format_duration(jitter)
		);

		bool ok = true;

		if (limits.max_loss_pct && loss_pct > *limits.max_loss_pct)
		{
			std::println(stderr, "FAIL: loss {}% > max {}%", loss_pct, *limits.max_loss_pct);
			ok = false;
		}

		if (limits.max_rtt && rtt_count > 0 && rtt > *limits.max_rtt)
		{
			std::println(stderr, "FAIL: rtt {} > max {}", format_duration(rtt), format_duration(*limits.max_rtt));
			ok = false;
		}

		if (!ok)
		{
			failed_ = true;
		}

		interval_start_ = now;
		return ok;
	}

private:

	std::atomic<uint64_t> out_packets_{0}, out_bytes_{0}, out_lost_{0};
	receive_stats receive_{};
	bool failed_ = false;
	steady_clock::time_point interval_start_ = steady_clock::now();
};
