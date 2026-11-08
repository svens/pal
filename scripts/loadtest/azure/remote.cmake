# Running commands and copying files over ssh. Included by common.cmake.

find_program(ssh_exe NAMES ssh)
find_program(scp_exe NAMES scp)
find_program(ssh_keygen_exe NAMES ssh-keygen)

function(ssh_control_socket out host) #{{{1
	set(${out} "${control_dir}/cm-${host}" PARENT_SCOPE)
endfunction()

function(ssh_drop_dead_control host) #{{{1
	# Until the path is gone, ssh keeps falling back to an unmultiplexed connection
	# instead of becoming master itself.
	ssh_control_socket(socket "${host}")
	if(NOT EXISTS "${socket}")
		return()
	endif()
	execute_process(
		COMMAND "${ssh_exe}" ${ssh_options} -O check "${host}"
		OUTPUT_QUIET
		ERROR_QUIET
		RESULT_VARIABLE alive
	)
	if(alive)
		file(REMOVE "${socket}")
	endif()
endfunction()

function(ssh_attempt out_status out_text out_error host script quiet) #{{{1
	# 255 is ssh's own failure and only a master that was there can have caused it,
	# so it is retried once. A command exiting 255 by itself costs one extra
	# attempt, an unreachable host none.
	ssh_drop_dead_control("${host}")
	ssh_control_socket(socket "${host}")
	set(had_master FALSE)
	if(EXISTS "${socket}")
		set(had_master TRUE)
	endif()

	foreach(attempt RANGE 1 2)
		if(quiet)
			execute_process(
				COMMAND "${ssh_exe}" ${ssh_options} "${host}" "${script}"
				OUTPUT_VARIABLE text
				OUTPUT_STRIP_TRAILING_WHITESPACE
				ERROR_VARIABLE error
				ERROR_STRIP_TRAILING_WHITESPACE
				RESULT_VARIABLE result
			)
		else()
			execute_process(
				COMMAND "${ssh_exe}" ${ssh_options} "${host}" "${script}"
				RESULT_VARIABLE result
			)
			set(text "")
			set(error "")
		endif()
		if(NOT result EQUAL 255 OR NOT had_master)
			break()
		endif()
		file(REMOVE "${socket}")
		set(had_master FALSE)
	endforeach()

	string(REPLACE "\r" "" text "${text}")
	string(REPLACE "\r" "" error "${error}")
	set(${out_status} "${result}" PARENT_SCOPE)
	set(${out_text} "${text}" PARENT_SCOPE)
	set(${out_error} "${error}" PARENT_SCOPE)
endfunction()

function(remote_run host script) #{{{1
	# Runs a command, printing what it prints, and fails the action on a nonzero
	# exit.
	ssh_attempt(result text error "${host}" "${script}" FALSE)
	if(result)
		message(FATAL_ERROR "${host}: '${script}' exit ${result}")
	endif()
endfunction()

function(remote_try out_status out_text host script) #{{{1
	# Captures stdout and reports the exit status, for probing.
	ssh_attempt(status text error "${host}" "${script}" TRUE)
	set(${out_status} "${status}" PARENT_SCOPE)
	set(${out_text} "${text}" PARENT_SCOPE)
endfunction()

function(remote_copy host dest) #{{{1
	# dest is relative to the login account's home directory.
	ssh_drop_dead_control("${host}")
	execute_process(
		COMMAND "${scp_exe}" ${ssh_options} -r ${ARGN} "${host}:${dest}"
		RESULT_VARIABLE result
	)
	if(result)
		message(FATAL_ERROR "${host}: copy failed (exit ${result})")
	endif()
endfunction()

function(remote_fetch host source dest) #{{{1
	# source is relative to the login account's home directory, dest is local.
	ssh_drop_dead_control("${host}")
	execute_process(
		COMMAND "${scp_exe}" ${ssh_options} "${host}:${source}" "${dest}"
		RESULT_VARIABLE result
	)
	if(result)
		message(FATAL_ERROR "${host}: fetching ${source} failed (exit ${result})")
	endif()
endfunction()
