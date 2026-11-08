## Run one measurement pass against the relay
include("${CMAKE_CURRENT_LIST_DIR}/harness.cmake")

run_once(pass ${streams})

if(pass_failures)
	message(FATAL_ERROR "${pass_failures} client process(es) exceeded max-loss=${max_loss} or max-rtt-ms=${max_rtt_ms}")
endif()

message(STATUS "pass: ${pass_sessions} sessions, ${pass_kpps} Kpps, ${pass_mbps} Mbps at the relay, ${pass_busy}% machine busy")
