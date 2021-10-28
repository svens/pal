list(APPEND samples_common_sources
	samples/command_line
	samples/metrics
	samples/thread
)

list(APPEND udp_relay_server_global_map
	samples/udp_relay_server_global_map.cpp
)
cxx_executable(udp_relay_server_global_map
	SOURCES ${udp_relay_server_global_map} ${samples_common_sources}
	LIBRARIES ${pal_libraries}
)
