# The az launcher and the queries every action shares. Included by common.cmake.

find_program(az_exe NAMES az az.cmd)

# execute_process does not go through a shell. On Windows az is a script beside
# az.cmd and CreateProcess starts neither, which surfaces as a RESULT of "unknown
# error" rather than an exit code. cmd takes it by bare name: a command line
# starting with a quote and holding more than two loses its outer pair, turning
# "C:/Program Files/..." into a command called C:/Program.
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
	set(az_launch cmd /c az)
else()
	set(az_launch "${az_exe}")
endif()

macro(az_check result) #{{{1
	if(${result})
		string(JOIN " " command ${ARGN})
		message(FATAL_ERROR "az ${command}: exit ${${result}}")
	endif()
endmacro()

function(az_read out) #{{{1
	# Runs az and captures stdout, failing the action on a nonzero exit.
	execute_process(
		COMMAND ${az_launch} ${ARGN}
		OUTPUT_VARIABLE text
		OUTPUT_STRIP_TRAILING_WHITESPACE
		RESULT_VARIABLE result
	)
	az_check(result ${ARGN})
	string(REPLACE "\r" "" text "${text}")
	set(${out} "${text}" PARENT_SCOPE)
endfunction()

function(az) #{{{1
	# Runs az for its effect, printing what it prints.
	execute_process(
		COMMAND ${az_launch} ${ARGN}
		RESULT_VARIABLE result
	)
	az_check(result ${ARGN})
endfunction()

function(vm_ids out) #{{{1
	# Resource ids of every VM in the group.
	az_read(text vm list --resource-group "${resource_group}" --query "[].id" --output tsv)
	string(REPLACE "\n" ";" ids "${text}")
	list(FILTER ids EXCLUDE REGEX "^$")
	if(NOT ids)
		message(FATAL_ERROR "no VMs in resource group '${resource_group}': deploy first")
	endif()
	set(${out} "${ids}" PARENT_SCOPE)
endfunction()
