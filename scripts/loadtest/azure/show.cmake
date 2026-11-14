## List cluster VMs with their addresses, and refresh .ssh/local.conf
include("${CMAKE_CURRENT_LIST_DIR}/common.cmake")

write_manifest()
write_ssh_config()

load_manifest()
foreach(host IN LISTS cluster_hosts)
	host_field(public "${host}" public)
	host_field(private "${host}" private)
	message(STATUS "${host}\t${public}\t${private}")
endforeach()
