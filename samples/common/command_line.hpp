#pragma once

#include <charconv>
#include <format>
#include <span>
#include <stdexcept>
#include <string_view>

template <typename OptionHandler>
void parse_command_line (std::span<const char *const> args, OptionHandler option_handler)
{
	if (args.empty())
	{
		return;
	}

	for (const auto *entry: args.subspan(1))
	{
		const std::string_view arg{entry};
		if (arg.starts_with("--"))
		{
			if (const auto assign = arg.find('='); assign != arg.npos)
			{
				option_handler(arg.substr(2, assign - 2), arg.substr(assign + 1));
			}
			else
			{
				option_handler(arg.substr(2), "");
			}
		}
		else
		{
			option_handler("", arg);
		}
	}
}

template <typename Int>
Int parse (std::string_view option, std::string_view argument)
{
	uint64_t value = 0;
	const auto *const first = argument.data();
	const auto *const last = first + argument.size();
	const auto [end, ec] = std::from_chars(first, last, value);
	if (ec != std::errc{} || end != last)
	{
		throw std::runtime_error(std::format("{}: not valid integer '{}'", option, argument));
	}
	return static_cast<Int>(value);
}
