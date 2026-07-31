# Deployment parameters, shared by validate.cmake and deploy.cmake

include("${CMAKE_CURRENT_LIST_DIR}/common.cmake")

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
