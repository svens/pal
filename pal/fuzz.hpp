#pragma once

#include <cstddef>
#include <format>
#include <vector>

namespace pal::fuzz
{

inline void init (int *argc, char ***argv, std::size_t max_len)
{
	static std::vector<char *> new_argv(*argv, *argv + *argc);

	static const auto max_len_arg = std::format("-max_len={}", max_len + max_len / 2);
	new_argv.push_back(const_cast<char *>(max_len_arg.c_str()));

	*argc = static_cast<int>(new_argv.size());
	new_argv.push_back(nullptr);
	*argv = new_argv.data();
}

} // namespace pal::fuzz
