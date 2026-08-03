# Shared configuration and helpers for the loadtest cluster actions. Included by
# every azure/*.cmake; not an action itself.

# paths {{{1
get_filename_component(azure_dir "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)
get_filename_component(loadtest_dir "${azure_dir}/.." ABSOLUTE)
get_filename_component(repo_root "${loadtest_dir}/../.." ABSOLUTE)
set(template_dir "${azure_dir}/template")

macro(default name) #{{{1
	# cluster.conf assigns through this rather than plain set(), so that -D on
	# the cmake command line wins: an include()d set() would overwrite it.
	if(NOT DEFINED ${name})
		set(${name} ${ARGN})
	endif()
endmacro()

# admin_user {{{1
# Prefix of the resource group and the login account on every VM. USER is unset
# under cmd.exe, which carries it in USERNAME instead.
if(DEFINED ENV{USER})
	default(admin_user "$ENV{USER}")
elseif(DEFINED ENV{USERNAME})
	default(admin_user "$ENV{USERNAME}")
else()
	message(FATAL_ERROR "cannot determine account name: pass -Dadmin_user=<name>")
endif()

include("${loadtest_dir}/cluster.conf")

# az launcher {{{1
find_program(az_exe NAMES az az.cmd)

# execute_process does not go through a shell, and on Windows az is not a real
# executable: wbin holds an extensionless script beside az.cmd and CreateProcess
# starts neither, which surfaces as a RESULT of "unknown error" rather than an
# exit code. Hence cmd -- and by bare name, because cmd strips the outer quote
# pair whenever its command line starts with a quote and holds more than two,
# unquoting "C:/Program Files/..." into a command called C:/Program.
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
	set(az_launch cmd /c az)
else()
	set(az_launch "${az_exe}")
endif()

# ssh paths {{{1
find_program(ssh_exe NAMES ssh)
find_program(scp_exe NAMES scp)
find_program(ssh_keygen_exe NAMES ssh-keygen)

# Config, host keys and the cluster keypair, written by deploy and removed by
# destroy, so their lifetime is the cluster's. Usable by hand as well:
#   ssh -F .ssh/local.conf relay-linux
set(ssh_dir "${loadtest_dir}/.ssh")
set(ssh_config "${ssh_dir}/local.conf")
set(ssh_key "${ssh_dir}/id_ed25519")
set(ssh_options -F "${ssh_config}")
set(cluster_manifest "${ssh_dir}/cluster.cmake")

# Control sockets sit outside ssh_dir because a unix socket path is limited to
# 104 bytes and this checkout is deep enough to exceed it. Removed by destroy.
if(DEFINED ENV{TMPDIR})
	set(tmp_dir "$ENV{TMPDIR}")
elseif(DEFINED ENV{TEMP})
	set(tmp_dir "$ENV{TEMP}")
else()
	set(tmp_dir "/tmp")
endif()
string(REGEX REPLACE "/+$" "" tmp_dir "${tmp_dir}")
default(control_dir "${tmp_dir}/${resource_group}")

set(remote_dir work)

function(az_command_text out) #{{{1
	# Joins an az argument list for display with the generated password masked.
	# The failure text reaches terminals, ninja logs and pasted bug reports, and
	# a deployment parameter list is the one place a secret passes through here.
	set(shown "")
	foreach(arg IN LISTS ARGN)
		if(arg MATCHES "^password=")
			list(APPEND shown "password=<redacted>")
		else()
			list(APPEND shown "${arg}")
		endif()
	endforeach()
	string(JOIN " " text ${shown})
	set(${out} "${text}" PARENT_SCOPE)
endfunction()

function(az) #{{{1
	# Runs az, failing the action on a nonzero exit.
	if(NOT az_exe)
		message(FATAL_ERROR "az not found: install the Azure CLI")
	endif()
	execute_process(
		COMMAND ${az_launch} ${ARGN}
		RESULT_VARIABLE result
	)
	if(result)
		az_command_text(command ${ARGN})
		message(FATAL_ERROR "az ${command}: exit ${result}")
	endif()
endfunction()

function(az_read out) #{{{1
	# Runs az and captures stdout.
	if(NOT az_exe)
		message(FATAL_ERROR "az not found: install the Azure CLI")
	endif()
	execute_process(
		COMMAND ${az_launch} ${ARGN}
		OUTPUT_VARIABLE text
		OUTPUT_STRIP_TRAILING_WHITESPACE
		RESULT_VARIABLE result
	)
	if(result)
		az_command_text(command ${ARGN})
		message(FATAL_ERROR "az ${command}: exit ${result}")
	endif()
	string(REPLACE "\r" "" text "${text}")
	set(${out} "${text}" PARENT_SCOPE)
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

function(resolve_login_ip out) #{{{1
	# Source address for the SSH/RDP rule. An empty or IPv6 answer would build a
	# rule that never matches the address SSH actually arrives from, so anything
	# unexpected is fatal rather than silently deployed.
	if(DEFINED login_ip)
		set(${out} "${login_ip}" PARENT_SCOPE)
		return()
	endif()

	if(DEFINED ENV{TMPDIR})
		set(scratch "$ENV{TMPDIR}/pal-loadtest-login-ip")
	elseif(DEFINED ENV{TEMP})
		set(scratch "$ENV{TEMP}/pal-loadtest-login-ip")
	else()
		set(scratch "/tmp/pal-loadtest-login-ip")
	endif()

	# An IPv4-only name, so a AAAA answer cannot slip through.
	file(DOWNLOAD "https://ipv4.icanhazip.com" "${scratch}" STATUS status TIMEOUT 15)
	list(GET status 0 code)
	if(NOT code EQUAL 0)
		list(GET status 1 reason)
		message(FATAL_ERROR "cannot determine login address (${reason}): pass -Dlogin_ip=<addr>")
	endif()

	file(READ "${scratch}" text)
	file(REMOVE "${scratch}")
	string(STRIP "${text}" text)
	if(NOT text MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+$")
		message(FATAL_ERROR "unexpected login address '${text}': pass -Dlogin_ip=<addr>")
	endif()
	set(${out} "${text}" PARENT_SCOPE)
endfunction()

function(ssh_public_key out) #{{{1
	# Public key installed on the Linux VMs. Generated per cluster and destroyed
	# with it, so no personal key ever reaches a throwaway VM and a leaked one
	# grants nothing once the VMs are gone. Passphraseless because every action
	# drives ssh unattended. -Dssh_public_key_file=<path> substitutes your own,
	# in which case ssh falls back to its usual identity search rather than the
	# generated key.
	if(DEFINED ssh_public_key_text)
		set(${out} "${ssh_public_key_text}" PARENT_SCOPE)
		return()
	endif()

	if(DEFINED ssh_public_key_file)
		set(path "${ssh_public_key_file}")
		if(NOT EXISTS "${path}")
			message(FATAL_ERROR "no such public key: ${path}")
		endif()
	else()
		if(NOT EXISTS "${ssh_key}")
			if(NOT ssh_keygen_exe)
				message(FATAL_ERROR "ssh-keygen not found: pass -Dssh_public_key_file=<path>")
			endif()
			file(MAKE_DIRECTORY "${ssh_dir}")
			execute_process(
				COMMAND "${ssh_keygen_exe}"
					-t ed25519
					-N ""
					-C "${resource_group}"
					-f "${ssh_key}"
				OUTPUT_QUIET
				RESULT_VARIABLE result
			)
			if(result)
				message(FATAL_ERROR "ssh-keygen failed: ${result}")
			endif()
			message(STATUS "generated ${ssh_key}")
		endif()
		set(path "${ssh_key}.pub")
	endif()

	file(READ "${path}" key)
	string(STRIP "${key}" key)
	# Guards against handing a private key to the template.
	if(NOT key MATCHES "^(ssh|ecdsa)-")
		message(FATAL_ERROR "${path} does not look like an OpenSSH public key")
	endif()
	set(${out} "${key}" PARENT_SCOPE)
endfunction()

function(make_password out) #{{{1
	# Composed rather than sampled, so that Azure's "three of four character
	# classes" rule cannot fail by luck.
	string(RANDOM
		LENGTH 20
		ALPHABET abcdefghijkmnpqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789
		body
	)
	set(${out} "Pal${body}9!" PARENT_SCOPE)
endfunction()

function(write_manifest) #{{{1
	# The one place that asks az what the cluster is. Everything downstream reads
	# the manifest it writes, so a sweep resolving each host per step spends no
	# lookups on facts that cannot change while the cluster lives.
	az_read(text vm list --show-details
		--resource-group "${resource_group}"
		--query "[].[name, storageProfile.osDisk.osType, publicIps, privateIps]"
		--output tsv
	)
	string(REPLACE "\n" ";" rows "${text}")
	list(FILTER rows EXCLUDE REGEX "^$")
	if(NOT rows)
		message(FATAL_ERROR "no VMs in resource group '${resource_group}': deploy first")
	endif()

	set(clients "")
	set(relays "")
	set(fields "")
	foreach(row IN LISTS rows)
		string(REPLACE "\t" ";" columns "${row}")
		list(GET columns 0 name)
		list(GET columns 1 os)
		list(GET columns 2 public)
		list(GET columns 3 private)
		if(name MATCHES "^loadtest-")
			list(APPEND clients "${name}")
		else()
			list(APPEND relays "${name}")
		endif()
		string(TOLOWER "${os}" os)
		string(MAKE_C_IDENTIFIER "${name}" id)
		string(APPEND fields "set(host_${id}_os ${os})\n")
		string(APPEND fields "set(host_${id}_public ${public})\n")
		string(APPEND fields "set(host_${id}_private ${private})\n")
	endforeach()
	list(SORT relays COMPARE NATURAL)
	list(SORT clients COMPARE NATURAL)
	string(JOIN " " relay_hosts ${relays})
	string(JOIN " " client_hosts ${clients})

	file(WRITE "${cluster_manifest}"
		"# generated for ${resource_group} -- regenerate with 'ninja show'\n"
		"set(cluster_relays ${relay_hosts})\n"
		"set(cluster_clients ${client_hosts})\n"
		"set(cluster_hosts \${cluster_relays} \${cluster_clients})\n"
		"${fields}"
	)
	message(STATUS "wrote ${cluster_manifest}")
endfunction()

macro(load_manifest) #{{{1
	# Defines cluster_hosts, cluster_clients, cluster_relays and, per VM, the os,
	# public and private fields that host_field reads. Taken from the generated
	# manifest rather than from client_count, so that the actions always address
	# the cluster that exists. client_count is a deploy-time parameter; repeating
	# it on every later action would be a second source of truth, and a stale one
	# would fail partway through with an unresolvable host.
	if(NOT EXISTS "${cluster_manifest}")
		message(FATAL_ERROR "no ${cluster_manifest}: deploy first, or run 'ninja show' to rebuild it")
	endif()
	include("${cluster_manifest}")
	if(NOT cluster_clients)
		message(FATAL_ERROR "no loadtest clients in ${cluster_manifest}")
	endif()
endmacro()

function(host_field out host field) #{{{1
	# One of os, public or private, for a host from the loaded manifest.
	string(MAKE_C_IDENTIFIER "${host}" id)
	if(NOT DEFINED host_${id}_${field})
		message(FATAL_ERROR "no ${field} for '${host}' in ${cluster_manifest}")
	endif()
	set(${out} "${host_${id}_${field}}" PARENT_SCOPE)
endfunction()

function(write_ssh_config) #{{{1
	load_manifest()
	string(JOIN " " all_hosts ${cluster_hosts})

	file(MAKE_DIRECTORY "${control_dir}")

	set(body "# generated for ${resource_group} -- regenerate with 'ninja show'\n")
	string(APPEND body "\nHost ${all_hosts}\n")
	string(APPEND body "\tUser ${admin_user}\n")
	string(APPEND body "\tUserKnownHostsFile ${ssh_dir}/known_hosts\n")
	string(APPEND body "\tStrictHostKeyChecking accept-new\n")
	string(APPEND body "\tBatchMode yes\n")
	string(APPEND body "\tConnectTimeout 10\n")
	string(APPEND body "\tControlMaster auto\n")
	string(APPEND body "\tControlPath ${control_dir}/cm-%n\n")
	string(APPEND body "\tControlPersist 10m\n")
	string(APPEND body "\tServerAliveInterval 5\n")
	string(APPEND body "\tServerAliveCountMax 3\n")
	if(EXISTS "${ssh_key}")
		string(APPEND body "\tIdentityFile ${ssh_key}\n")
		string(APPEND body "\tIdentitiesOnly yes\n")
	endif()

	foreach(host IN LISTS cluster_hosts)
		host_field(address "${host}" public)
		string(APPEND body "\nHost ${host}\n\tHostName ${address}\n")
	endforeach()

	file(WRITE "${ssh_config}" "${body}")
	message(STATUS "wrote ${ssh_config}")
endfunction()

function(ssh_control_socket out host) #{{{1
	set(${out} "${control_dir}/cm-${host}" PARENT_SCOPE)
endfunction()

function(ssh_drop_dead_control host) #{{{1
	# Until the path is gone, ssh keeps falling back to an unmultiplexed
	# connection rather than becoming master itself.
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
	# 255 is ssh's own failure rather than the remote command's, and only a master
	# that was there can have caused it. A command exiting 255 by itself therefore
	# costs one extra attempt, and an unreachable host none.
	if(NOT ssh_exe)
		message(FATAL_ERROR "ssh not found")
	endif()
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
	ssh_attempt(result text error "${host}" "${script}" FALSE)
	if(result)
		message(FATAL_ERROR "${host}: '${script}' exit ${result}")
	endif()
endfunction()

function(remote_probe out_status out_text out_error host script) #{{{1
	# As remote_run, but captures both streams and reports the exit status instead
	# of failing, for probing. An unreachable host is described on stderr only.
	ssh_attempt(status text error "${host}" "${script}" TRUE)
	set(${out_status} "${status}" PARENT_SCOPE)
	set(${out_text} "${text}" PARENT_SCOPE)
	set(${out_error} "${error}" PARENT_SCOPE)
endfunction()

function(remote_try out_status out_text host script) #{{{1
	# remote_probe for the callers that have nothing to say about a failure.
	remote_probe(status text ignored "${host}" "${script}")
	set(${out_status} "${status}" PARENT_SCOPE)
	set(${out_text} "${text}" PARENT_SCOPE)
endfunction()

function(remote_copy host dest) #{{{1
	# dest is relative to the login account's home directory.
	if(NOT scp_exe)
		message(FATAL_ERROR "scp not found")
	endif()
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
	# source is relative to the login account's home directory, dest is a local path
	if(NOT scp_exe)
		message(FATAL_ERROR "scp not found")
	endif()
	ssh_drop_dead_control("${host}")
	execute_process(
		COMMAND "${scp_exe}" ${ssh_options} "${host}:${source}" "${dest}"
		RESULT_VARIABLE result
	)
	if(result)
		message(FATAL_ERROR "${host}: fetching ${source} failed (exit ${result})")
	endif()
endfunction()

function(seconds_now out) #{{{1
	string(TIMESTAMP now "%s" UTC)
	set(${out} "${now}" PARENT_SCOPE)
endfunction()

function(wait_for_provisioning) #{{{1
	# The cap is elapsed time: a poll is itself an ssh per host with a connect
	# timeout, so the sleeps alone are a fraction of the wall clock.
	load_manifest()
	set(pending "")
	foreach(host IN LISTS cluster_hosts)
		host_field(os "${host}" os)
		if(os STREQUAL "linux")
			list(APPEND pending "${host}")
		endif()
	endforeach()

	seconds_now(started)
	set(waited 0)
	set(reasons "")
	while(pending AND waited LESS 900)
		set(still "")
		set(reasons "")
		foreach(host IN LISTS pending)
			remote_probe(status text error "${host}" "cloud-init status")
			if(NOT status EQUAL 0 OR NOT text MATCHES "done")
				list(APPEND still "${host}")
				if(status EQUAL 0)
					set(why "${text}")
				elseif(error)
					set(why "${error}")
				else()
					set(why "ssh exited ${status} without a message")
				endif()
				# A semicolon in the text would split the list element it becomes.
				string(REGEX REPLACE "[\r\n;]+" " " why "${why}")
				list(APPEND reasons "${host}: ${why}")
			endif()
		endforeach()
		set(pending "${still}")
		seconds_now(now)
		math(EXPR waited "${now} - ${started}")
		if(pending AND waited LESS 900)
			execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 10)
			seconds_now(now)
			math(EXPR waited "${now} - ${started}")
		endif()
	endwhile()
	if(pending)
		string(JOIN "\n  " detail ${reasons})
		message(FATAL_ERROR "not provisioned after ${waited}s\n  ${detail}")
	endif()
	message(STATUS "provisioned")
endfunction()

function(relay_command out host) #{{{1
	# Shared by the start-relay action and by sweep, so that a ladder cannot
	# measure a differently configured relay than the one started by hand.
	host_field(os "${host}" os)
	string(JOIN " " args ${relay_args})
	if(os STREQUAL "windows")
		set(${out} "${remote_dir}\\udp_relay_mturn.exe ${args}" PARENT_SCOPE)
	else()
		set(${out} "./${remote_dir}/udp_relay_mturn ${args}" PARENT_SCOPE)
	endif()
endfunction()

function(relay_pid out host) #{{{1
	# Empty when no relay is running there.
	remote_try(status pid "${host}" "pidof udp_relay_mturn")
	if(NOT status EQUAL 0)
		set(pid "")
	endif()
	set(${out} "${pid}" PARENT_SCOPE)
endfunction()

function(start_relay host log) #{{{1
	# Detached, with its output kept: without a terminal the log is the only
	# record of what the relay itself saw. Readiness is the bound socket rather
	# than the live process -- clients started in between would lose their first
	# datagrams to a closed port and charge them to loss.
	relay_command(command "${host}")
	remote_run("${host}" "nohup ${command} >${remote_dir}/${log} 2>&1 &")

	set(waited 0)
	while(waited LESS 30)
		remote_try(status ignored "${host}" "ss -lunp | grep -q udp_relay_mturn")
		if(status EQUAL 0)
			return()
		endif()
		execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 1)
		math(EXPR waited "${waited} + 1")
	endwhile()
	message(FATAL_ERROR "${host}: relay bound no socket in ${waited}s, see ${remote_dir}/${log}")
endfunction()

function(stop_relay host) #{{{1
	remote_try(status ignored "${host}" "pkill -x udp_relay_mturn")
	set(waited 0)
	while(waited LESS 30)
		relay_pid(pid "${host}")
		if(NOT pid)
			return()
		endif()
		execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 1)
		math(EXPR waited "${waited} + 1")
	endwhile()
	message(FATAL_ERROR "${host}: relay ${pid} still running after ${waited}s")
endfunction()
