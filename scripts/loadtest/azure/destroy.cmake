## Delete the resource group and everything in it
include("${CMAKE_CURRENT_LIST_DIR}/common.cmake")

# cmake script mode cannot read a confirmation from the terminal, so it arrives as
# an argument. Naming the group doubles as the check that az is pointed at the
# intended subscription.
if(NOT DEFINED confirm OR NOT confirm STREQUAL "${resource_group}")
	message(FATAL_ERROR
		"refusing to delete without confirmation.\n"
		"This removes '${resource_group}' and everything in it -- every VM, "
		"disk, address and the network -- from whichever subscription az is "
		"currently using. To go ahead:\n"
		"  cmake -Dconfirm=${resource_group} -P azure/destroy.cmake"
	)
endif()

message(STATUS "deleting ${resource_group}, this takes a few minutes")
az(group delete --name "${resource_group}" --yes)
file(REMOVE_RECURSE "${ssh_dir}" "${control_dir}")
message(STATUS "deleted ${resource_group}")
