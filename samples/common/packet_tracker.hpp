#pragma once

#include <array>
#include <chrono>
#include <mutex>
#include <optional>

using std::chrono::steady_clock;

template <size_t WindowSize>
class packet_tracker
{
public:

	[[nodiscard]] uint64_t sent (uint64_t packet_id, steady_clock::time_point now)
	{
		const std::lock_guard lock{mutex_};
		auto &slot = window_[packet_id % WindowSize];
		const uint64_t lost = slot.packet_id.has_value() ? 1 : 0;
		slot = {.packet_id = packet_id, .send_time = now};
		return lost;
	}

	std::optional<steady_clock::duration> received (uint64_t packet_id, steady_clock::time_point now)
	{
		const std::lock_guard lock{mutex_};
		auto &slot = window_[packet_id % WindowSize];
		if (!slot.packet_id || *slot.packet_id != packet_id)
		{
			return std::nullopt;
		}
		const auto latency = now - slot.send_time;
		slot.packet_id.reset();
		return latency;
	}

private:

	struct entry
	{
		std::optional<uint64_t> packet_id{};
		steady_clock::time_point send_time{};
	};

	std::array<entry, WindowSize> window_{};
	std::mutex mutex_{};
};
