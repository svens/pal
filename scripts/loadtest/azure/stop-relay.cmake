## Stop the relay
include("${CMAKE_CURRENT_LIST_DIR}/common.cmake")

if(NOT DEFINED host)
	message(FATAL_ERROR "no host: run this through 'ninja stop-relay-linux' or 'ninja stop-relay-windows'")
endif()

load_manifest()
list(FIND cluster_relays "${host}" index)
if(index LESS 0)
	message(FATAL_ERROR "'${host}' is not a relay in this cluster: ${cluster_relays}")
endif()

relay_pid(pid "${host}")
if(NOT pid)
	message(STATUS "${host}: no relay running")
	return()
endif()

stop_relay("${host}")
message(STATUS "${host}: stopped ${pid}")
