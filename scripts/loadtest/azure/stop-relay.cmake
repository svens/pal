## Stop the relay
include("${CMAKE_CURRENT_LIST_DIR}/common.cmake")

default(host ${relay_host})

relay_pid(pid "${host}")
if(NOT pid)
	message(STATUS "${host}: no relay running")
	return()
endif()

stop_relay("${host}")
message(STATUS "${host}: stopped ${pid}")
