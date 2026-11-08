## Start every VM in the cluster
include("${CMAKE_CURRENT_LIST_DIR}/common.cmake")

vm_ids(ids)
az(vm start --ids ${ids})
