# One measurement pass, shared by run.cmake and sweep.cmake.

include("${CMAKE_CURRENT_LIST_DIR}/common.cmake")

# Checked here rather than in run_once, which sweep reaches only after starting a
# relay: an out-of-range runtime would abort with that relay left running.
if(runtime LESS 5)
	message(FATAL_ERROR "runtime=${runtime} is too short a counter window to measure")
endif()

function(counters_field out rows kind name) #{{{1
	# A value from the rows host/counters writes: "<kind>\t<name>\t<value>". It
	# exits nonzero rather than omitting a figure it was asked for, so an absent
	# row is a broken contract and not a measurement worth carrying on with.
	if(NOT rows MATCHES "(^|\n)${kind}\t${name}\t([^\n]*)")
		message(FATAL_ERROR "counters delta reported no ${kind} ${name}:\n${rows}")
	endif()
	set(${out} "${CMAKE_MATCH_2}" PARENT_SCOPE)
endfunction()

function(counters_faults out rows) #{{{1
	# The fault rows, each already carrying its display text.
	set(faults "")
	string(REPLACE "\n" ";" lines "${rows}")
	foreach(line IN LISTS lines)
		if(line MATCHES "^fault\t(.+)$")
			list(APPEND faults "${CMAKE_MATCH_1}")
		endif()
	endforeach()
	set(${out} "${faults}" PARENT_SCOPE)
endfunction()

function(run_once out streams) #{{{1
	# ARGV2, when given, is a command started on the relay just after the first
	# counter snapshot, expected to stop itself inside the window.
	set(during_window "${ARGV2}")

	# Launches the clients, waits for them, and reports the relay-side counter
	# deltas for the same window. Sets, in the caller's scope:
	#
	#   ${out}_sessions    clients times streams, the load this pass carried
	#   ${out}_failures    client processes that exited nonzero, i.e. saw a
	#                      threshold violation
	#   ${out}_kpps        relay receive rate over the window
	#   ${out}_mbps
	#   ${out}_busy        percentage of the relay machine's CPU the window used
	#   ${out}_packet_ns   what one datagram cost that machine
	#   ${out}_faults      host-side movements that make the step unrepresentative
	#
	# The relay is expected to be running already: it is the side you watch, so it
	# belongs in a tmux session rather than under this script.
	load_manifest()

	relay_pid(pid ${relay_host})
	if(NOT pid)
		message(FATAL_ERROR "no ${relay_process} on ${relay_host}: start it first with 'ninja start-relay'")
	endif()

	host_field(relay_private ${relay_host} private)
	list(LENGTH cluster_clients count)
	client_sessions(total ${streams})
	message(STATUS "${count} clients x ${streams} streams = ${total} sessions")

	# Starting a capture costs the relay a burst of drops. The harness starts it
	# when its own warm-up sleep returns, so the clients are given five seconds more
	# and the burst lands before any of them applies a threshold.
	set(client_warmup "${warmup}")
	if(during_window)
		math(EXPR client_warmup "${warmup} + 5")
	endif()

	set(args
		"--server=${relay_private}"
		"--streams=${streams}"
		"--kbps=${kbps}"
		"--payload=${payload}"
		"--warmup=${client_warmup}"
		"--runtime=${runtime}"
		"--max-loss=${max_loss}"
		"--max-rtt-ms=${max_rtt_ms}"
	)
	string(JOIN " " arg_line ${args})

	seconds_now(launched_first)
	foreach(client IN LISTS cluster_clients)
		# Detached, so every client VM generates load concurrently: cmake script mode
		# has no way to run several execute_process calls at once.
		remote_run("${client}"
			"nohup ${remote_dir}/host/run-clients ${arg_line} >/dev/null 2>&1 &"
		)
	endforeach()
	seconds_now(launched_last)

	# Every client keeps its own clock, so all of them are inside their measured
	# window only between the last one leaving warm-up and the first one finishing:
	# runtime shortened by the launch skew.
	math(EXPR skew "${launched_last} - ${launched_first}")
	math(EXPR window "${runtime} - ${skew}")
	if(window LESS 5)
		message(FATAL_ERROR "launching ${count} clients took ${skew}s, leaving a ${window}s counter window: cluster is too slow to measure at runtime=${runtime}")
	endif()

	# The counters bracket the measured window and not the warm-up before it, the
	# same split the clients make.
	execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep ${warmup})
	remote_run(${relay_host} "sudo ${remote_dir}/host/counters snapshot ${remote_dir}/counters.before")

	# Detached, because this call has to return for the window to be the window.
	if(during_window)
		remote_run(${relay_host} "nohup ${during_window} >${remote_dir}/during_window.log 2>&1 &")
	endif()

	execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep ${window})
	remote_run(${relay_host} "sudo ${remote_dir}/host/counters snapshot ${remote_dir}/counters.after")

	# loadtest stops itself after warm-up plus --runtime, so the marker appears on
	# its own; the grace period covers handshake retries and startup skew. Elapsed
	# rather than the sum of the sleeps: a poll is one ssh per client.
	math(EXPR limit "${client_warmup} + ${runtime} + 120")
	seconds_now(started)
	set(waited 0)
	set(pending "${cluster_clients}")
	while(pending AND waited LESS limit)
		execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 5)
		set(still "")
		foreach(host IN LISTS pending)
			remote_try(status ignored "${host}" "test -f ${remote_dir}/done")
			if(NOT status EQUAL 0)
				list(APPEND still "${host}")
			endif()
		endforeach()
		set(pending "${still}")
		seconds_now(now)
		math(EXPR waited "${now} - ${started}")
	endwhile()
	if(pending)
		message(WARNING "clients still running after ${waited}s: ${pending}")
	endif()

	set(failures 0)
	foreach(host IN LISTS cluster_clients)
		remote_try(status codes "${host}" "cat ${remote_dir}/run.status")
		string(REPLACE "\n" ";" code_lines "${codes}")
		list(FILTER code_lines EXCLUDE REGEX "^$")
		foreach(code IN LISTS code_lines)
			if(NOT code EQUAL 0)
				math(EXPR failures "${failures} + 1")
			endif()
		endforeach()
		remote_try(status tail_text "${host}" "tail -n 2 ${remote_dir}/run.log")
		message(STATUS "${host}\n${tail_text}")
	endforeach()

	ssh_attempt(status rows error ${relay_host}
		"${remote_dir}/host/counters delta ${remote_dir}/counters.before ${remote_dir}/counters.after"
		TRUE
	)
	if(NOT status EQUAL 0)
		message(FATAL_ERROR "${relay_host}: counters delta exit ${status}\n${error}")
	endif()
	message(STATUS "relay counters\n${rows}")

	counters_field(kpps "${rows}" rate kpps)
	counters_field(mbps "${rows}" rate mbps)
	counters_field(busy "${rows}" cpu busy_pct)
	counters_field(packet_ns "${rows}" cost ns_per_packet)
	counters_faults(faults "${rows}")

	if(faults)
		# One call rather than one per fault: each message(WARNING) carries its own
		# file and call-stack header, so a loop would bury the content.
		string(JOIN "\n  " detail ${faults})
		message(WARNING
			"not a service measurement: the load never reached the relay, or the "
			"host was not given the CPU to process it\n  ${detail}"
		)
	endif()

	set(${out}_sessions "${total}" PARENT_SCOPE)
	set(${out}_failures "${failures}" PARENT_SCOPE)
	set(${out}_kpps "${kpps}" PARENT_SCOPE)
	set(${out}_mbps "${mbps}" PARENT_SCOPE)
	set(${out}_busy "${busy}" PARENT_SCOPE)
	set(${out}_packet_ns "${packet_ns}" PARENT_SCOPE)
	set(${out}_faults "${faults}" PARENT_SCOPE)
endfunction()
