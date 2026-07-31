## Check that the templates and parameters would deploy

# Placeholders for the two values validation does not look at: the template
# treats the key as an opaque string and the address as an NSG source, while
# what is being checked is sizes, counts, ports and resource schemas. Supplying
# them here keeps validate from generating a keypair and from depending on a
# third-party address lookup, neither of which a check should need. The
# parameter set still comes from deploy_args, so the two cannot drift apart.
#
# TEST-NET-3 (RFC 5737) and a keypair whose private half was discarded.
set(login_ip 203.0.113.1)
set(ssh_public_key_text
	"ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIGbOYCcNw+El+PDrBMH+afiSxg1cn2ujxDbzJKPAocap placeholder"
)

include("${CMAKE_CURRENT_LIST_DIR}/deploy_args.cmake")

az(deployment sub validate
	--name "${deployment_name}"
	--location "${location}"
	--template-file "${template_dir}/main.bicep"
	--parameters ${deploy_args}
)
