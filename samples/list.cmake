list(APPEND samples_common_sources
	samples/command_line
	samples/metrics
	samples/thread
)

# udp_relay_client {{{1
cxx_executable(udp_relay_client
	SOURCES ${samples_common_sources}
		samples/udp_relay_client.cpp
	LIBRARIES pal::pal
)

# udp_relay_server_global_map {{{1
cxx_executable(udp_relay_server_global_map
	SOURCES ${samples_common_sources}
		samples/udp_relay_server_global_map.cpp
	LIBRARIES pal::pal
)

# udp_relay_server_local_map {{{1
cxx_executable(udp_relay_server_local_map
	SOURCES ${samples_common_sources}
		samples/udp_relay_server_local_map.cpp
	LIBRARIES pal::pal
)

# udp_relay_server_multi_send {{{1
cxx_executable(udp_relay_server_multi_send
	SOURCES ${samples_common_sources}
		samples/udp_relay_server_multi_send.cpp
	LIBRARIES pal::pal
)
