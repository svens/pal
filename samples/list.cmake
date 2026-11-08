list(APPEND samples_common_sources
	samples/common/command_line.hpp
	samples/common/metric.hpp
	samples/common/packet_tracker.hpp
	samples/common/relay.hpp
	samples/common/statistics.hpp
	samples/common/thread.hpp
)

list(APPEND udp_relay_sources
	samples/udp_relay/main.cpp
)

list(APPEND loadtest_sources
	samples/loadtest/main.cpp
)

list(APPEND samples_sources
	${samples_common_sources}
	${udp_relay_sources}
	${loadtest_sources}
)
