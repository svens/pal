# Preflight, capture passes and per-pass budget, shared by the profile actions.
# Not an action itself.

include("${CMAKE_CURRENT_LIST_DIR}/harness.cmake")

set(profile_state "${profile_dir}/state.cmake")

macro(profile_preflight) #{{{1
	# Sets stamp, and the deployment, threads, sessions and conntrack a comparison
	# is only valid within.
	if(profile_seconds GREATER_EQUAL runtime)
		message(FATAL_ERROR
			"profile_seconds=${profile_seconds} does not fit inside runtime=${runtime}: "
			"the capture runs within the measured window, which is runtime minus the client launch skew"
		)
	endif()

	find_program(stackcollapse_exe NAMES stackcollapse-perf.pl)
	if(NOT stackcollapse_exe)
		message(FATAL_ERROR "stackcollapse-perf.pl not on PATH: FlameGraph is a dependency, see docs/profile.md")
	endif()

	find_program(awk_exe NAMES awk)
	if(NOT awk_exe)
		message(FATAL_ERROR "awk not found: the stage classifier is tools/stage_budget.awk")
	endif()

	load_manifest()

	set(streams "${profile_streams}")
	set(relay_args "${profile_relay_args}")

	relay_pid(pid relay-linux)
	if(pid)
		message(FATAL_ERROR
			"relay already running on relay-linux as ${pid}, so its build is unknown; stop it with\n"
			"$ ninja stop-relay-linux"
		)
	endif()

	remote_run(relay-linux "sudo ${remote_dir}/perf/prepare apply")

	execute_process(
		COMMAND git -C "${repo_root}" rev-parse --short HEAD
		OUTPUT_VARIABLE commit
		OUTPUT_STRIP_TRAILING_WHITESPACE
		ERROR_QUIET
		RESULT_VARIABLE git_status
	)
	if(git_status OR NOT commit)
		set(commit "nocommit")
	endif()

	remote_try(status loaded relay-linux "lsmod | grep -c nf_conntrack || true")
	if(NOT status EQUAL 0)
		message(FATAL_ERROR "cannot read the relay's loaded modules")
	endif()

	if(profile_conntrack)
		set(conntrack "conntrack")
		if(loaded EQUAL 0)
			message(FATAL_ERROR
				"profile_conntrack is ON but nf_conntrack is not loaded on relay-linux: "
				"see docs/tune_linux_azure.md, 4 The guest agent's netfilter table"
			)
		endif()
	else()
		set(conntrack "no_conntrack")
		if(loaded GREATER 0)
			message(FATAL_ERROR
				"profile_conntrack is OFF but nf_conntrack is loaded on relay-linux: "
				"see docs/tune_linux_azure.md, 4 The guest agent's netfilter table"
			)
		endif()
	endif()

	list(LENGTH cluster_clients client_total)
	math(EXPR sessions "${client_total} * ${streams}")
	string(JOIN " " relay_arg_text ${relay_args})
	if(relay_arg_text MATCHES "--threads=([0-9]+)")
		set(threads "${CMAKE_MATCH_1}")
	else()
		set(threads "default")
	endif()

	remote_try(status deployment relay-linux
		"curl -s -H Metadata:true \"http://169.254.169.254/metadata/instance/compute/vmId?api-version=2021-02-01&format=text\""
	)
	if(NOT status EQUAL 0 OR NOT deployment)
		message(FATAL_ERROR "cannot read the relay's vmId from IMDS")
	endif()

	remote_try(status binary_hash relay-linux "sha256sum ${remote_dir}/udp_relay_mturn | cut -c1-12")
	if(NOT status EQUAL 0 OR NOT binary_hash)
		message(FATAL_ERROR "cannot hash ${remote_dir}/udp_relay_mturn on relay-linux: ship first")
	endif()

	string(TIMESTAMP captured_at "%Y%m%dT%H%M%S" UTC)

	set(stamp "${commit}-threads_${threads}-sessions_${sessions}-exe_${binary_hash}-${captured_at}")
	file(MAKE_DIRECTORY "${profile_dir}")
endmacro()

macro(profile_capture passes) #{{{1
	# Requires profile_preflight. Leaves ${passes} captures under profile_dir, each
	# as a folded file, a markdown budget and its tab-separated sibling.
	message(STATUS "profiling ${stamp}: ${relay_arg_text}")

	foreach(pass RANGE 1 ${passes})
		set(label "${stamp}-${pass}")
		message(CHECK_START "capture ${pass} of ${passes}")
		start_relay(relay-linux "relay-profile-${pass}.log")

		# Read by run_once, which starts it just after the first counter snapshot.
		set(during_window "sudo ${remote_dir}/perf/capture ${profile_seconds} ${label} ${profile_freq}")
		run_once(failures kpps mbps busy faults packet_ns ${streams})
		unset(during_window)

		if(failures)
			message(CHECK_FAIL "thresholds exceeded")
			message(FATAL_ERROR
				"${failures} client process(es) violated the thresholds at ${sessions} sessions."
				"The relay is left running for examination: stop it with\n"
				"$ ninja stop-relay-linux"
			)
		endif()
		if(faults)
			string(JOIN "\n  " detail ${faults})
			message(WARNING "host faults during capture ${pass}, so this profile is not a service measurement:\n"
				"  ${detail}"
			)
		endif()

		remote_run(relay-linux "${remote_dir}/perf/script ${label}")
		remote_fetch(relay-linux "${remote_dir}/perf/perf-${label}.txt.gz" "${profile_dir}/${label}.txt.gz")
		remote_fetch(relay-linux "${remote_dir}/perf/perf-${label}.syscalls" "${profile_dir}/${label}.syscalls")

		stop_relay(relay-linux)

		if(packet_ns STREQUAL "?")
			message(CHECK_FAIL "no packet cost")
			message(FATAL_ERROR "the relay's counters did not yield a per-packet cost, so there is no budget to divide")
		endif()

		set(packet_ns_${pass} "${packet_ns}")
		message(CHECK_PASS "${kpps} Kpps, ${busy}% busy, ${packet_ns} ns/packet")
	endforeach()

	foreach(pass RANGE 1 ${passes})
		set(label "${stamp}-${pass}")
		set(ns_per_packet "${packet_ns_${pass}}")

		execute_process(
			COMMAND gzip -dc "${profile_dir}/${label}.txt.gz"
			COMMAND "${stackcollapse_exe}"
			OUTPUT_FILE "${profile_dir}/${label}.folded"
			RESULT_VARIABLE fold_status
		)
		if(fold_status)
			message(FATAL_ERROR "folding ${label} failed: ${fold_status}")
		endif()

		foreach(format markdown tsv)
			if(format STREQUAL "markdown")
				set(budget_file "${profile_dir}/${label}.budget")
			else()
				set(budget_file "${profile_dir}/${label}.budget.tsv")
			endif()
			execute_process(
				COMMAND "${awk_exe}" -f "${loadtest_dir}/tools/stage_budget.awk"
					-v ns_per_packet=${ns_per_packet}
					-v format=${format}
					"${profile_dir}/${label}.folded"
				OUTPUT_FILE "${budget_file}"
				RESULT_VARIABLE budget_status
			)
			if(budget_status)
				message(FATAL_ERROR "classifying ${label} as ${format} failed: ${budget_status}")
			endif()
		endforeach()

		file(READ "${profile_dir}/${label}.budget" budget)
		message(STATUS "${label}: ${ns_per_packet} ns/packet\n${budget}")
	endforeach()

	message(STATUS "artifacts in ${profile_dir}")
endmacro()
