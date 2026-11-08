## Shut down every VM in the cluster (compute billing continues)
include("${CMAKE_CURRENT_LIST_DIR}/common.cmake")

vm_ids(ids)
az(vm stop --ids ${ids})
