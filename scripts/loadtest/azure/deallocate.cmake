## Deallocate every VM in the cluster (stops compute billing)
include("${CMAKE_CURRENT_LIST_DIR}/common.cmake")

vm_ids(ids)
az(vm deallocate --ids ${ids})
