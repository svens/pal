# Paths, cluster configuration and the generated cluster manifest. Included by
# every azure/*.cmake; not an action itself.

# paths {{{1
get_filename_component(azure_dir "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)
get_filename_component(loadtest_dir "${azure_dir}/.." ABSOLUTE)
get_filename_component(repo_root "${loadtest_dir}/../.." ABSOLUTE)
set(template_dir "${azure_dir}/template")

macro(default name) #{{{1
	# -D on the command line wins: an include()d set() would overwrite it.
	if(NOT DEFINED ${name})
		set(${name} ${ARGN})
	endif()
endmacro()

# admin_user {{{1
# Prefix of the resource group and the login account on every VM. USER is unset
# under cmd.exe, which carries USERNAME instead.
if(DEFINED ENV{USER})
	default(admin_user "$ENV{USER}")
elseif(DEFINED ENV{USERNAME})
	default(admin_user "$ENV{USERNAME}")
else()
	message(FATAL_ERROR "cannot determine account name: pass -Dadmin_user=<name>")
endif()

include("${loadtest_dir}/cluster.conf")

# temporary directory {{{1
if(DEFINED ENV{TMPDIR})
	set(tmp_dir "$ENV{TMPDIR}")
elseif(DEFINED ENV{TEMP})
	set(tmp_dir "$ENV{TEMP}")
else()
	set(tmp_dir "/tmp")
endif()
string(REGEX REPLACE "/+$" "" tmp_dir "${tmp_dir}")

# ssh paths {{{1
# Config, host keys and the cluster keypair, written by deploy and removed by
# destroy. Usable by hand:
#   ssh -F .ssh/local.conf relay-linux
set(ssh_dir "${loadtest_dir}/.ssh")
set(ssh_config "${ssh_dir}/local.conf")
set(ssh_key "${ssh_dir}/id_ed25519")
set(ssh_options -F "${ssh_config}")
set(cluster_manifest "${ssh_dir}/cluster.cmake")

# A unix socket path is capped at 104 bytes and ssh binds ControlPath under a
# temporary name consuming 17 of them, which a path beside a deep checkout
# exceeds. Removed by destroy.
default(control_dir "${tmp_dir}/${resource_group}")

set(remote_dir work)

include("${azure_dir}/az.cmake")
include("${azure_dir}/remote.cmake")
include("${azure_dir}/relay.cmake")

function(seconds_now out) #{{{1
	string(TIMESTAMP now "%s" UTC)
	set(${out} "${now}" PARENT_SCOPE)
endfunction()

function(write_manifest) #{{{1
	# The one place that asks az what the cluster is; everything downstream reads
	# what this writes.
	az_read(text vm list --show-details
		--resource-group "${resource_group}"
		--query "[].[name, publicIps, privateIps]"
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
		list(GET columns 1 public)
		list(GET columns 2 private)
		if(name MATCHES "^loadtest-")
			list(APPEND clients "${name}")
		else()
			list(APPEND relays "${name}")
		endif()
		string(MAKE_C_IDENTIFIER "${name}" id)
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
	# Defines cluster_hosts, cluster_clients, cluster_relays and, per VM, the
	# public and private fields host_field reads. client_count is a deploy-time
	# parameter; the manifest is what the later actions address.
	if(NOT EXISTS "${cluster_manifest}")
		message(FATAL_ERROR "no ${cluster_manifest}: deploy first, or run 'ninja show' to rebuild it")
	endif()
	include("${cluster_manifest}")
	if(NOT cluster_clients)
		message(FATAL_ERROR "no loadtest clients in ${cluster_manifest}")
	endif()
endmacro()

function(host_field out host field) #{{{1
	# The public or private address of a host from the loaded manifest.
	string(MAKE_C_IDENTIFIER "${host}" id)
	set(${out} "${host_${id}_${field}}" PARENT_SCOPE)
endfunction()

function(client_sessions out streams) #{{{1
	# Total sessions the cluster carries at ${streams} per client.
	load_manifest()
	list(LENGTH cluster_clients count)
	math(EXPR total "${count} * ${streams}")
	set(${out} "${total}" PARENT_SCOPE)
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

	ssh_control_socket(control_pattern "%n")
	string(APPEND body "\tControlMaster auto\n")
	string(APPEND body "\tControlPath ${control_pattern}\n")
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

function(wait_for_provisioning) #{{{1
	# Capped on elapsed time: poll is an ssh per host with a connect timeout.
	load_manifest()
	set(pending ${cluster_hosts})

	seconds_now(started)
	set(waited 0)
	set(reasons "")
	while(pending AND waited LESS 900)
		set(still "")
		set(reasons "")
		foreach(host IN LISTS pending)
			ssh_attempt(status text error "${host}" "cloud-init status" TRUE)
			if(NOT status EQUAL 0 OR NOT text MATCHES "done")
				list(APPEND still "${host}")
				if(status EQUAL 0)
					set(why "${text}")
				elseif(error)
					set(why "${error}")
				else()
					set(why "ssh exited ${status} without a message")
				endif()
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
