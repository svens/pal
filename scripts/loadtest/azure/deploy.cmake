## Create or update the cluster
include("${CMAKE_CURRENT_LIST_DIR}/deploy_args.cmake")

message(STATUS "${resource_group} in ${location}, login from ${login_address}")

az(deployment sub create
	--name "${deployment_name}"
	--location "${location}"
	--template-file "${template_dir}/main.bicep"
	--parameters ${deploy_args}
)

write_manifest()
write_ssh_config()
wait_for_provisioning()
