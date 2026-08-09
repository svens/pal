# Starting, finding and stopping the relay under test. Included by common.cmake.

set(relay_process udp_relay_mturn)
set(relay_host relay-linux)

function(remote_wait out host script seconds) #{{{1
	# Polls a remote command once a second until it succeeds. Sets ${out} TRUE when
	# it did, FALSE when the time ran out.
	set(waited 0)
	while(waited LESS ${seconds})
		remote_try(status ignored "${host}" "${script}")
		if(status EQUAL 0)
			set(${out} TRUE PARENT_SCOPE)
			return()
		endif()
		execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 1)
		math(EXPR waited "${waited} + 1")
	endwhile()
	set(${out} FALSE PARENT_SCOPE)
endfunction()

function(relay_command out host) #{{{1
	# Shared by the start-relay action and by sweep, so a ladder cannot measure a
	# differently configured relay than one started by hand.
	host_field(os "${host}" os)
	string(JOIN " " args ${relay_args})
	if(os STREQUAL "windows")
		set(${out} "${remote_dir}\\${relay_process}.exe ${args}" PARENT_SCOPE)
	else()
		set(${out} "./${remote_dir}/${relay_process} ${args}" PARENT_SCOPE)
	endif()
endfunction()

function(relay_pid out host) #{{{1
	# Empty when no relay is running there.
	remote_try(status pid "${host}" "pidof ${relay_process}")
	if(NOT status EQUAL 0)
		set(pid "")
	endif()
	set(${out} "${pid}" PARENT_SCOPE)
endfunction()

function(start_relay host log) #{{{1
	# Detached, with its output kept: without a terminal the log is the only record
	# of what the relay saw. Readiness is the bound socket -- clients started before
	# it lose their first datagrams to a closed port and charge them to loss.
	relay_command(command "${host}")
	remote_run("${host}" "nohup ${command} >${remote_dir}/${log} 2>&1 &")

	remote_wait(bound "${host}" "ss -lunp | grep -q ${relay_process}" 30)
	if(NOT bound)
		message(FATAL_ERROR "${host}: relay bound no socket in 30s, see ${remote_dir}/${log}")
	endif()
endfunction()

function(stop_relay host) #{{{1
	remote_try(status ignored "${host}" "pkill -x ${relay_process}")
	remote_wait(gone "${host}" "! pidof ${relay_process} >/dev/null" 30)
	if(NOT gone)
		relay_pid(pid "${host}")
		message(FATAL_ERROR "${host}: relay ${pid} still running after 30s")
	endif()
endfunction()
