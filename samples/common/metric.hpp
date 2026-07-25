#pragma once

#include <chrono>
#include <format>
#include <span>
#include <string_view>

using namespace std::chrono;

struct traffic_metric
{
	struct dir
	{
		size_t packets;
		size_t bytes;

		void inc (size_t bytes_transferred) noexcept
		{
			packets++;
			bytes += bytes_transferred;
		}
	};
	dir in{}, out{};

	void reset () noexcept
	{
		in.packets = in.bytes = out.packets = out.bytes = 0;
	}

	traffic_metric &load (traffic_metric &dest) noexcept
	{
		dest.in = in;
		dest.out = out;
		return *this;
	}

	traffic_metric &sum (traffic_metric &dest) noexcept
	{
		dest.in.packets += in.packets;
		dest.in.bytes += in.bytes;
		dest.out.packets += out.packets;
		dest.out.bytes += out.bytes;
		return *this;
	}
};

constexpr uint64_t per_second (uint64_t amount, milliseconds interval) noexcept
{
	if (interval.count() > 0)
	{
		return amount * 1'000 / interval.count();
	}
	return 0;
}

struct bit_rate
{
	uint64_t per_sec;

	constexpr bit_rate (size_t bytes, milliseconds interval) noexcept
		: per_sec{per_second(8 * bytes, interval)}
	{
	}
};

struct packet_rate
{
	uint64_t per_sec;

	constexpr packet_rate (size_t packets, milliseconds interval) noexcept
		: per_sec{per_second(packets, interval)}
	{
	}
};

namespace __metric
{

struct rate_formatter
{
	static constexpr double unit_scale_precision_limit = 100.0;

	template <typename ParseContext>
	constexpr ParseContext::iterator parse (ParseContext &ctx)
	{
		return ctx.begin();
	}

	template <typename FormatContext>
	FormatContext::iterator
	format (uint64_t per_sec, std::span<const std::string_view> units, FormatContext &ctx) const
	{
		auto value = static_cast<double>(per_sec);
		auto unit = units.begin();
		while (value >= 1'000.0 && unit + 1 != units.end())
		{
			value /= 1'000.0;
			++unit;
		}
		if (unit != units.begin() && value < unit_scale_precision_limit)
		{
			return std::format_to(ctx.out(), "{:.1f} {}", value, *unit);
		}
		return std::format_to(ctx.out(), "{:.0f} {}", value, *unit);
	}
};

} // namespace __metric

template <>
struct std::formatter<bit_rate>: __metric::rate_formatter
{
	template <typename FormatContext>
	FormatContext::iterator format (const bit_rate &rate, FormatContext &ctx) const
	{
		static constexpr auto units = std::to_array<std::string_view>({"bps", "Kbps", "Mbps", "Gbps"});
		return rate_formatter::format(rate.per_sec, units, ctx);
	}
};

template <>
struct std::formatter<packet_rate>: __metric::rate_formatter
{
	template <typename FormatContext>
	FormatContext::iterator format (const packet_rate &rate, FormatContext &ctx) const
	{
		static constexpr auto units = std::to_array<std::string_view>({"pps", "Kpps", "Mpps"});
		return rate_formatter::format(rate.per_sec, units, ctx);
	}
};
