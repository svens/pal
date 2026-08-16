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

	relay_pid(pid ${relay_host})
	if(pid)
		message(FATAL_ERROR
			"relay already running on ${relay_host} as ${pid}, so its build is unknown; stop it with\n"
			"$ ninja stop-relay"
		)
	endif()

	remote_run(${relay_host} "sudo ${remote_dir}/perf/prepare apply")

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

	remote_try(status loaded ${relay_host} "lsmod | grep -c nf_conntrack || true")
	if(NOT status EQUAL 0)
		message(FATAL_ERROR "cannot read the relay's loaded modules")
	endif()

	if(profile_conntrack)
		set(conntrack "conntrack")
		if(loaded EQUAL 0)
			message(FATAL_ERROR
				"profile_conntrack is ON but nf_conntrack is not loaded on ${relay_host}: "
				"see docs/tune_linux_azure.md, The guest agent's netfilter table"
			)
		endif()
	else()
		set(conntrack "no_conntrack")
		if(loaded GREATER 0)
			message(FATAL_ERROR
				"profile_conntrack is OFF but nf_conntrack is loaded on ${relay_host}: "
				"see docs/tune_linux_azure.md, The guest agent's netfilter table"
			)
		endif()
	endif()

	client_sessions(sessions "${streams}")
	string(JOIN " " relay_arg_text ${relay_args})
	if(relay_arg_text MATCHES "--threads=([0-9]+)")
		set(threads "${CMAKE_MATCH_1}")
	else()
		set(threads "default")
	endif()

	remote_try(status deployment ${relay_host}
		"curl -s -H Metadata:true \"http://169.254.169.254/metadata/instance/compute/vmId?api-version=2021-02-01&format=text\""
	)
	if(NOT status EQUAL 0 OR NOT deployment)
		message(FATAL_ERROR "cannot read the relay's vmId from IMDS")
	endif()

	remote_try(status binary_hash ${relay_host} "sha256sum ${remote_dir}/${relay_process} | cut -c1-12")
	if(NOT status EQUAL 0 OR NOT binary_hash)
		message(FATAL_ERROR "cannot hash ${remote_dir}/${relay_process} on ${relay_host}: ship first")
	endif()

	string(TIMESTAMP captured_at "%Y%m%dT%H%M%S" UTC)

	set(stamp "${commit}-threads_${threads}-sessions_${sessions}-exe_${binary_hash}-${captured_at}")
	file(MAKE_DIRECTORY "${profile_dir}")
endmacro()

macro(profile_capture passes) #{{{1
	# Requires profile_preflight. Leaves ${passes} captures under profile_dir, each
	# as a folded file, a markdown budget, its tab-separated sibling and the relay's
	# own log.
	message(STATUS "profiling ${stamp}: ${relay_arg_text}")

	foreach(pass RANGE 1 ${passes})
		set(label "${stamp}-${pass}")
		message(CHECK_START "capture ${pass} of ${passes}")
		start_relay(${relay_host} "relay-profile-${pass}.log")

		# Started by run_once just after the first counter snapshot.
		run_once(capture ${streams}
			"sudo ${remote_dir}/perf/capture ${profile_seconds} ${label} ${profile_freq}"
		)

		if(capture_failures)
			message(CHECK_FAIL "thresholds exceeded")
			message(FATAL_ERROR
				"${capture_failures} client process(es) violated the thresholds at ${sessions} sessions."
				"The relay is left running for examination: stop it with\n"
				"$ ninja stop-relay"
			)
		endif()

		remote_run(${relay_host} "${remote_dir}/perf/script ${label}")
		stop_relay(${relay_host})

		remote_fetch(${relay_host} "${remote_dir}/perf/perf-${label}.txt.gz" "${profile_dir}/${label}.txt.gz")
		remote_fetch(${relay_host} "${remote_dir}/perf/perf-${label}.syscalls" "${profile_dir}/${label}.syscalls")
		remote_fetch(${relay_host} "${remote_dir}/relay-profile-${pass}.log" "${profile_dir}/${label}.relay.log")

		set(packet_ns_${pass} "${capture_packet_ns}")
		message(CHECK_PASS "${capture_kpps} Kpps, ${capture_busy}% busy, ${capture_packet_ns} ns/packet")
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
