## Ramp load until the thresholds break, reporting the last passing point
include("${CMAKE_CURRENT_LIST_DIR}/harness.cmake")

# The ramp needs no output parsing: loadtest's exit code is the only contract it
# offers, and --max-loss / --max-rtt-ms turn it into a pass/fail oracle.

load_manifest()
relay_pid(pid ${relay_host})
if(pid)
	message(FATAL_ERROR
		"relay already running on ${relay_host} as ${pid}, so its state is unknown "
		"-- a previous sweep may have aborted. Stop it with 'ninja stop-relay'"
	)
endif()

if(NOT relay_restart)
	start_relay(${relay_host} relay.log)
endif()

set(passed 0)
set(broke FALSE)
set(streams_at ${sweep_start})
while(streams_at LESS_EQUAL ${sweep_max})
	message(CHECK_START "sweep: ${streams_at} streams per client")
	if(relay_restart)
		start_relay(${relay_host} "relay-${streams_at}.log")
	endif()

	run_once(step ${streams_at})

	# A relay that died during the step is the result, not a step to restart past.
	relay_pid(pid ${relay_host})
	if(NOT pid)
		message(CHECK_FAIL "relay exited")
		message(FATAL_ERROR "relay died during the ${streams_at} step: see ${remote_dir}/relay-${streams_at}.log on ${relay_host}")
	endif()

	if(relay_restart)
		stop_relay(${relay_host})
	endif()

	if(step_failures)
		message(CHECK_FAIL "thresholds exceeded")
		set(broke TRUE)
		break()
	endif()
	message(CHECK_PASS "ok")
	# Kept per step, so the reported figures belong to the last point that passed
	# rather than to the one that broke.
	set(passed ${streams_at})
	set(passed_kpps ${step_kpps})
	set(passed_mbps ${step_mbps})
	set(passed_busy "${step_busy}")
	set(passed_faults "${step_faults}")
	math(EXPR streams_at "${streams_at} + ${sweep_step}")
endwhile()

if(NOT relay_restart)
	stop_relay(${relay_host})
endif()

if(passed EQUAL 0)
	message(FATAL_ERROR "no passing point: ${sweep_start} streams per client already violates the thresholds")
endif()

client_sessions(sessions ${passed})
if(broke)
	message(STATUS "knee: ${passed} streams per client passes and ${streams_at} does not, so ${sessions} sessions is the limit")
else()
	message(STATUS
		"range exhausted: every step through sweep_max=${sweep_max} passed, so "
		"${sessions} sessions is a floor and not a measured limit -- raise sweep_max"
	)
endif()
message(STATUS "relay rx at that point: ${passed_kpps} Kpps, ${passed_mbps} Mbps")
message(STATUS "machine busy at that point: ${passed_busy}%")
message(STATUS "divide by relay vCPU count for the sessions/core figure docs/results.md wants")

# Repeated here because a sweep prints twenty step reports and nobody rereads
# them: a fault at the reported point would otherwise reach the results table
# looking like a valid row.
if(passed_faults)
	string(JOIN "\n  " detail ${passed_faults})
	message(WARNING "the reported point had host faults, so it is not a service limit:\n  ${detail}")
endif()
