## Create or update the cluster
include("${CMAKE_CURRENT_LIST_DIR}/deploy_args.cmake")

message(STATUS "${resource_group} in ${location}, login from ${login_address}")

az(deployment sub create
	--name "${deployment_name}"
	--location "${location}"
	--template-file "${template_dir}/main.bicep"
	--parameters ${deploy_args}
)

# Printed once and never stored. The Linux VMs take the SSH key instead, so this
# is only usable where there is no alternative.
if(deploy_relay_windows)
	message(STATUS "relay-windows password: ${admin_password}")
	message(STATUS "rotate with: az vm user update --resource-group ${resource_group} --name relay-windows --username ${admin_user} --password <new>")
endif()

write_manifest()
write_ssh_config()
wait_for_provisioning()
