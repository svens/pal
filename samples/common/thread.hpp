#pragma once

#include <pal/version.hpp>
#include <cstddef>

#if __pal_os_linux
	#include <sched.h>
#endif

namespace this_thread
{

inline void pin_to_cpu (size_t id) noexcept
{
	#if __pal_os_linux
	{
		cpu_set_t cpu_set;
		CPU_ZERO(&cpu_set);
		CPU_SET(static_cast<int>(id), &cpu_set);
		::sched_setaffinity(0, sizeof(cpu_set), &cpu_set);
	}
	#else
	{
		(void)id;
	}
	#endif
}

} // namespace this_thread
