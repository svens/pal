# One measurement pass, shared by run.cmake and sweep.cmake.

include("${CMAKE_CURRENT_LIST_DIR}/common.cmake")

function(snapshot_value out text key) #{{{1
	if(text MATCHES "(^|\n)${key} ([^\n]*)")
		set(${out} "${CMAKE_MATCH_2}" PARENT_SCOPE)
	else()
		set(${out} "" PARENT_SCOPE)
	endif()
endfunction()

function(uptime_centiseconds out value) #{{{1
	# /proc/uptime is "<seconds>.<hundredths>", so joining the two halves gives
	# hundredths and keeps the arithmetic integral -- cmake has no floating
	# point. Concatenated as a string rather than multiplied, to avoid feeding a
	# leading-zero fraction to math().
	if(value MATCHES "^([0-9]+)\\.([0-9][0-9])$")
		set(${out} "${CMAKE_MATCH_1}${CMAKE_MATCH_2}" PARENT_SCOPE)
	else()
		set(${out} "" PARENT_SCOPE)
	endif()
endfunction()

function(format_tenths out value) #{{{1
	# 26 becomes "2.6". Rates are carried as tenths because truncating to whole
	# Kpps understates a slow run badly: 2215 pps reads as "2 Kpps".
	math(EXPR whole "${value} / 10")
	math(EXPR frac "${value} % 10")
	set(${out} "${whole}.${frac}" PARENT_SCOPE)
endfunction()

function(relay_rx_rate out_kpps out_mbps before after) #{{{1
	# Receive rate the relay actually saw, from its own counters. Packets come
	# from UdpInDatagrams, which is the workload exactly; octets have no UDP-level
	# equivalent, so they come from IpExtInOctets, which is IP level and counts
	# the host's other traffic too -- ssh and ARP, both negligible here.
	set(${out_kpps} "?" PARENT_SCOPE)
	set(${out_mbps} "?" PARENT_SCOPE)

	snapshot_value(uptime_before "${before}" time.uptime)
	snapshot_value(uptime_after "${after}" time.uptime)
	uptime_centiseconds(cs_before "${uptime_before}")
	uptime_centiseconds(cs_after "${uptime_after}")
	if(NOT cs_before OR NOT cs_after)
		return()
	endif()
	math(EXPR elapsed_cs "${cs_after} - ${cs_before}")
	if(elapsed_cs LESS_EQUAL 0)
		return()
	endif()

	snapshot_value(packets_before "${before}" nstat.UdpInDatagrams)
	snapshot_value(packets_after "${after}" nstat.UdpInDatagrams)
	snapshot_value(octets_before "${before}" nstat.IpExtInOctets)
	snapshot_value(octets_after "${after}" nstat.IpExtInOctets)
	if(NOT packets_after OR NOT octets_after)
		return()
	endif()

	# Tenths of a Kpps reduces to packets over centiseconds:
	# pps = P * 100 / cs, so Kpps * 10 = P / cs.
	math(EXPR kpps_tenths "(${packets_after} - ${packets_before}) / ${elapsed_cs}")
	math(EXPR mbps_tenths "(${octets_after} - ${octets_before}) * 8 / ${elapsed_cs} / 1000")
	format_tenths(kpps "${kpps_tenths}")
	format_tenths(mbps "${mbps_tenths}")
	set(${out_kpps} "${kpps}" PARENT_SCOPE)
	set(${out_mbps} "${mbps}" PARENT_SCOPE)
endfunction()

function(relay_packet_cost out before after) #{{{1
	# Nanoseconds of machine CPU one received datagram cost.
	set(${out} "?" PARENT_SCOPE)

	snapshot_value(clk_tck "${after}" meta.clk_tck)
	if(clk_tck STREQUAL "" OR clk_tck LESS_EQUAL 0)
		return()
	endif()

	set(jiffies 0)
	set(idle 0)
	set(iowait 0)
	foreach(key user nice system idle iowait irq softirq steal)
		snapshot_value(before_value "${before}" cpu.${key})
		snapshot_value(after_value "${after}" cpu.${key})
		if(after_value STREQUAL "")
			return()
		endif()
		if(before_value STREQUAL "")
			set(before_value 0)
		endif()
		math(EXPR delta "${after_value} - ${before_value}")
		math(EXPR jiffies "${jiffies} + ${delta}")
		if(key STREQUAL "idle")
			set(idle "${delta}")
		elseif(key STREQUAL "iowait")
			set(iowait "${delta}")
		endif()
	endforeach()
	math(EXPR busy_jiffies "${jiffies} - ${idle} - ${iowait}")

	snapshot_value(packets_before "${before}" nstat.UdpInDatagrams)
	snapshot_value(packets_after "${after}" nstat.UdpInDatagrams)
	if(packets_after STREQUAL "" OR packets_before STREQUAL "")
		return()
	endif()
	math(EXPR packets "${packets_after} - ${packets_before}")

	if(busy_jiffies LESS_EQUAL 0 OR packets LESS_EQUAL 0)
		return()
	endif()
	math(EXPR ns "${busy_jiffies} * 1000000000 / (${clk_tck} * ${packets})")
	set(${out} "${ns}" PARENT_SCOPE)
endfunction()

function(relay_faults out out_busy before after) #{{{1
	# Only counters meaning the packet never reached the relay. Two exclusions are
	# deliberate: UdpRcvbufErrors is the service saturating, which is the thing
	# being measured, and softnet.squeezed is normal at high packet rates.
	set(faults "")
	set(cpu_total 0)
	set(cpu_steal 0)
	set(cpu_idle 0)
	set(cpu_iowait 0)

	string(REPLACE "\n" ";" lines "${after}")
	foreach(line IN LISTS lines)
		if(NOT line MATCHES "^([a-z]+\\.[^ ]+) ([0-9]+)$")
			continue()
		endif()
		set(key "${CMAKE_MATCH_1}")
		snapshot_value(previous "${before}" "${key}")
		if(NOT previous)
			set(previous 0)
		endif()
		math(EXPR delta "${CMAKE_MATCH_2} - ${previous}")

		if(key MATCHES "^cpu\\.")
			math(EXPR cpu_total "${cpu_total} + ${delta}")
			if(key STREQUAL "cpu.steal")
				set(cpu_steal "${delta}")
			elseif(key STREQUAL "cpu.idle")
				set(cpu_idle "${delta}")
			elseif(key STREQUAL "cpu.iowait")
				set(cpu_iowait "${delta}")
			endif()
		elseif(key MATCHES "^nic\\." OR key STREQUAL "softnet.dropped")
			if(delta GREATER 0)
				list(APPEND faults "${key} +${delta}")
			endif()
		endif()
	endforeach()

	# A few stolen jiffies are ordinary on a shared host and warning about them
	# every run would train the warning away. Only a whole percent or more means
	# the hypervisor took enough to make the numbers fiction.
	set(busy "?")
	if(cpu_total GREATER 0)
		math(EXPR steal_pct "${cpu_steal} * 100 / ${cpu_total}")
		if(steal_pct GREATER 0)
			list(APPEND faults "cpu.steal ${steal_pct}%")
		endif()

		# The same expression 'counters delta' prints as machine.cpu, computed here
		# rather than parsed back out of it: two derivations of one figure would
		# eventually disagree, and this one has to survive into the knee report.
		# Process CPU is not a substitute -- it cannot see softirq, and the gap
		# closes as load rises, so it is least trustworthy where it would be used.
		math(EXPR busy "(${cpu_total} - ${cpu_idle} - ${cpu_iowait}) * 100 / ${cpu_total}")
	endif()

	set(${out} "${faults}" PARENT_SCOPE)
	set(${out_busy} "${busy}" PARENT_SCOPE)
endfunction()

function(run_once out_failures out_kpps out_mbps out_busy out_faults out_packet_ns streams) #{{{1
	# Launches the clients, waits for them, and prints the relay-side counter
	# deltas for the same window. Sets ${out_failures} to the number of client
	# processes that exited nonzero, i.e. that saw a threshold violation,
	# ${out_kpps} / ${out_mbps} to the relay's measured receive rate over the same
	# window, ${out_busy} to the percentage of the relay machine's CPU that window
	# consumed, ${out_faults} to any host-side drops that make the step
	# unrepresentative, and ${out_packet_ns} to what one datagram cost that machine.
	#
	# The relay is expected to be running already: it is the side you watch, so it
	# belongs in a tmux session rather than under this script.
	remote_try(status pid relay-linux "pidof udp_relay_mturn")
	if(NOT status EQUAL 0 OR NOT pid)
		message(FATAL_ERROR "no udp_relay_mturn on relay-linux: start it first with 'ninja start-relay-linux'")
	endif()

	load_manifest()
	host_field(relay_private relay-linux private)
	list(LENGTH cluster_clients count)
	math(EXPR total "${count} * ${streams}")
	message(STATUS "${count} clients x ${streams} streams = ${total} sessions")

	# The counters bracket the measured window and not the warm-up before it, the
	# same split the clients make: handshake, page faults and the pacing token
	# bucket are load the run is not asking about, and a rate averaged over them
	# comes out low by whatever share of the run they took.
	if(runtime LESS 5)
		message(FATAL_ERROR "runtime=${runtime} is too short a counter window to measure")
	endif()

	# Starting a capture costs the relay a burst of drops. The harness starts it
	# when its own warm-up sleep returns, so the clients are given five seconds
	# more and the burst lands before any of them applies a threshold. Launch skew
	# alone decides which side of the boundary it falls on otherwise.
	set(client_warmup "${warmup}")
	if(DEFINED during_window)
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
		# Detached, so that every client VM generates load concurrently: cmake
		# script mode has no way to run several execute_process calls at once.
		remote_run("${client}"
			"nohup ${remote_dir}/host/run-clients ${arg_line} >/dev/null 2>&1 &"
		)
	endforeach()
	seconds_now(launched_last)

	# Launching is one ssh per client, so the clients keep their own clocks and
	# every one of them is inside its measured window only between the last one
	# leaving warm-up and the first one finishing: the run shortened by the launch
	# skew. Counting the whole of ${runtime} instead would average the relay's rate
	# over a tail where the earliest clients had already stopped.
	math(EXPR skew "${launched_last} - ${launched_first}")
	math(EXPR window "${runtime} - ${skew}")
	if(window LESS 5)
		message(FATAL_ERROR "launching ${count} clients took ${skew}s, leaving a ${window}s counter window: cluster is too slow to measure at runtime=${runtime}")
	endif()

	execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep ${warmup})
	remote_run(relay-linux "sudo ${remote_dir}/host/counters snapshot ${remote_dir}/counters.before")

	# Anything the caller wants measured over the same seconds as the counters,
	# started here and expected to stop itself within the window: a profile that
	# spans a different interval than the packet counts cannot be divided by them.
	# Detached, because this call has to return for the window to be the window.
	if(DEFINED during_window)
		remote_run(relay-linux "nohup ${during_window} >${remote_dir}/during_window.log 2>&1 &")
	endif()

	execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep ${window})
	remote_run(relay-linux "sudo ${remote_dir}/host/counters snapshot ${remote_dir}/counters.after")

	# loadtest stops itself after warm-up plus --runtime, so the marker should
	# appear on its own; the grace period covers handshake retries and startup
	# skew. Elapsed rather than the sum of the sleeps: a poll is one ssh per
	# client, and on a slow cluster that is the larger half.
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
		remote_try(status text "${host}" "cat ${remote_dir}/run.status")
		string(REPLACE "\n" ";" codes "${text}")
		list(FILTER codes EXCLUDE REGEX "^$")
		foreach(code IN LISTS codes)
			if(NOT code EQUAL 0)
				math(EXPR failures "${failures} + 1")
			endif()
		endforeach()
		remote_try(status text "${host}" "tail -n 2 ${remote_dir}/run.log")
		message(STATUS "${host}\n${text}")
	endforeach()

	message(STATUS "relay counters")
	remote_run(relay-linux
		"${remote_dir}/host/counters delta ${remote_dir}/counters.before ${remote_dir}/counters.after"
	)

	# The snapshots are read again rather than parsed out of the delta above:
	# their "key value" form is a contract, the delta's layout is for humans.
	remote_try(status before_text relay-linux "cat ${remote_dir}/counters.before")
	remote_try(status after_text relay-linux "cat ${remote_dir}/counters.after")
	relay_rx_rate(kpps mbps "${before_text}" "${after_text}")
	message(STATUS "relay rx: ${kpps} Kpps, ${mbps} Mbps")

	relay_faults(faults busy "${before_text}" "${after_text}")
	relay_packet_cost(packet_ns "${before_text}" "${after_text}")
	message(STATUS "machine busy: ${busy}%")
	if(faults)
		# One call rather than one per fault: each message(WARNING) carries its
		# own file and call-stack header, so a loop would bury the content.
		string(JOIN "\n  " detail ${faults})
		message(WARNING
			"not a service measurement: the load never reached the relay, or the "
			"host was not given the CPU to process it\n  ${detail}"
		)
	endif()

	set(${out_failures} "${failures}" PARENT_SCOPE)
	set(${out_kpps} "${kpps}" PARENT_SCOPE)
	set(${out_mbps} "${mbps}" PARENT_SCOPE)
	set(${out_busy} "${busy}" PARENT_SCOPE)
	set(${out_faults} "${faults}" PARENT_SCOPE)
	set(${out_packet_ns} "${packet_ns}" PARENT_SCOPE)
endfunction()
