## Start the relay and attach to it
include("${CMAKE_CURRENT_LIST_DIR}/common.cmake")

default(host ${relay_host})
load_manifest()

host_field(os "${host}" os)
remote_try(session ignored "${host}" "tmux has-session -t relay")
if(session EQUAL 0 AND NOT os STREQUAL "windows")
	set(command "tmux attach -t relay")
else()
	relay_pid(pid "${host}")
	if(pid)
		message(FATAL_ERROR "relay already running on ${host} as ${pid}, outside tmux: stop it with 'ninja stop-relay'")
	endif()
	relay_command(command "${host}")
	if(NOT os STREQUAL "windows")
		set(command "tmux new-session -s relay ${command}")
	endif()
endif()

execute_process(
	COMMAND "${ssh_exe}" ${ssh_options} -t "${host}" "${command}"
	RESULT_VARIABLE result
)
if(result)
	message(FATAL_ERROR "${host}: exit ${result}")
endif()
