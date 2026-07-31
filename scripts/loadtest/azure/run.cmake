## Run one measurement pass against the relay
include("${CMAKE_CURRENT_LIST_DIR}/harness.cmake")

if(runtime LESS_EQUAL 0)
	message(FATAL_ERROR "runtime must be positive: an unbounded run never returns here")
endif()

run_once(failures kpps mbps busy faults ${streams})

if(failures)
	message(FATAL_ERROR "${failures} client process(es) exceeded max-loss=${max_loss} or max-rtt-ms=${max_rtt_ms}")
endif()

load_manifest()
list(LENGTH cluster_clients count)
math(EXPR sessions "${count} * ${streams}")
message(STATUS "pass: ${sessions} sessions, ${kpps} Kpps, ${mbps} Mbps at the relay, ${busy}% machine busy")
if(faults)
	string(JOIN "\n  " detail ${faults})
	message(WARNING "host faults during the run, so this is not a service measurement:\n  ${detail}")
endif()
