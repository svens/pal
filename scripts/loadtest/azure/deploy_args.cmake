# Deployment parameters and the credentials they carry, shared by validate.cmake
# and deploy.cmake.

include("${CMAKE_CURRENT_LIST_DIR}/common.cmake")

function(resolve_login_ip out) #{{{1
	# Source address for the SSH/RDP rule. An empty or IPv6 answer would build a
	# rule that never matches the address SSH arrives from, so anything unexpected
	# is fatal.
	if(DEFINED login_ip)
		set(${out} "${login_ip}" PARENT_SCOPE)
		return()
	endif()

	# An IPv4-only name, so a AAAA answer cannot slip through.
	set(scratch "${tmp_dir}/pal-loadtest-login-ip")
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
	# Public key installed on the Linux VMs, generated per cluster and destroyed
	# with it. Passphraseless, since every action drives ssh unattended.
	# -Dssh_public_key_file=<path> substitutes your own, in which case ssh falls
	# back to its usual identity search.
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
	# Composed, so Azure's three-of-four-character-classes rule cannot fail by luck.
	string(RANDOM
		LENGTH 20
		ALPHABET abcdefghijkmnpqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789
		body
	)
	set(${out} "Pal${body}9!" PARENT_SCOPE)
endfunction()

# parameters {{{1
resolve_login_ip(login_address)
ssh_public_key(public_key)

# Bicep wants a JSON boolean, not a CMake truth value.
if(deploy_relay_windows)
	set(with_windows true)
else()
	set(with_windows false)
endif()

set(deploy_args
	"location=${location}"
	"login_ip=${login_address}"
	"username=${admin_user}"
	"ssh_public_key=${public_key}"
	"relay_size=${relay_size}"
	"client_size=${client_size}"
	"client_count=${client_count}"
	"deploy_relay_windows=${with_windows}"
	"client_ports=${client_ports}"
	"peer_ports=${peer_ports}"
)

if(deploy_relay_windows)
	make_password(admin_password)
	list(APPEND deploy_args "password=${admin_password}")
endif()
